// SMCDellDDV: Dell fan RPM for VirtualSMC via the firmware's DDV WMI method.
// Newer Dell laptops (e.g. XPS 17 9720) no longer answer the legacy SMM fan
// interface used by SMCDellSensors. Their DSDT still implements Dell's DDV WMI
// interface (GUID 8A42EA14-4F2A-FD45-6422-0087F7A7E608) in plain AML on top of
// the EC mailbox. Method ID 0x20 ("fan sensor information") returns
//   Package { length, Buffer { type, rpm_lo, rpm_hi, ..., 0xFF } }
// (same layout Linux's dell-wmi-ddv uses). This driver only *reads*: it
// evaluates WMDV(0, 0x20, 0) on its own work loop every 2 s and publishes
// FNum / FnAc / FnID / FnMn / FnMx so HWMonitorSMC2 and friends show the fans.
// Discovery is deferred until the EC is available (see discover()).
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include <VirtualSMCSDK/kern_vsmcapi.hpp>

namespace {
constexpr size_t MaxFans=8;
constexpr uint32_t PollMs=2000;
constexpr uint32_t DiscoverTries=60; // 2 minutes
constexpr const char *KeyIndexes="0123456789ABCDEF";
constexpr SMC_KEY KeyFNum=SMC_MAKE_IDENTIFIER('F','N','u','m');
constexpr SMC_KEY KeyF0Ac(size_t i) {return SMC_MAKE_IDENTIFIER('F',KeyIndexes[i],'A','c');}
constexpr SMC_KEY KeyF0ID(size_t i) {return SMC_MAKE_IDENTIFIER('F',KeyIndexes[i],'I','D');}
constexpr SMC_KEY KeyF0Mn(size_t i) {return SMC_MAKE_IDENTIFIER('F',KeyIndexes[i],'M','n');}
constexpr SMC_KEY KeyF0Mx(size_t i) {return SMC_MAKE_IDENTIFIER('F',KeyIndexes[i],'M','x');}
constexpr int32_t DiagFunctionStrLen=12;
struct FanTypeDesc {uint8_t type {0},zone {1},location {4},rsvd {0};char function[DiagFunctionStrLen] {};};
static_assert(sizeof(FanTypeDesc)==16,"SMC FDS layout");

volatile uint16_t fanRPM[MaxFans] {};
volatile uint16_t fanMax[MaxFans] {};

class FanAc : public VirtualSMCValue {
    size_t index;
protected:
    SMC_RESULT readAccess() override {
        *reinterpret_cast<uint16_t *>(data)=VirtualSMCAPI::encodeIntFp(SmcKeyTypeFpe2,fanRPM[index]);
        return SmcSuccess;
    }
public:
    explicit FanAc(size_t i) : index(i) {}
};
class FanMx : public VirtualSMCValue {
    size_t index;
protected:
    SMC_RESULT readAccess() override {
        *reinterpret_cast<uint16_t *>(data)=VirtualSMCAPI::encodeIntFp(SmcKeyTypeFpe2,fanMax[index]);
        return SmcSuccess;
    }
public:
    explicit FanMx(size_t i) : index(i) {}
};
} // namespace

class SMCDellDDV : public IOService {
    OSDeclareDefaultStructors(SMCDellDDV)
    IOACPIPlatformDevice *acpi {nullptr};
    IOWorkLoop *loop {nullptr};
    IOTimerEventSource *timer {nullptr};
    IONotifier *vsmcNotifier {nullptr};
    VirtualSMCAPI::Plugin vsmcPlugin {"SMCDellDDV",parseModuleVersion("1.0.1"),VirtualSMCAPI::Version};
    // Returns fan count (0 on failure); fills types/rpm.
    size_t fans {0};
    uint32_t tries {0};
    size_t query(uint8_t *types,uint16_t *rpm,size_t max);
    bool discover();
    void poll();
    static bool vsmcNotificationHandler(void *sensors,void *refCon,IOService *vsmc,IONotifier *notifier);
public:
    IOService *probe(IOService *provider,SInt32 *score) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
};
OSDefineMetaClassAndStructors(SMCDellDDV,IOService)

size_t SMCDellDDV::query(uint8_t *types,uint16_t *rpm,size_t max) {
    if(!acpi)return 0;
    uint32_t zero=0;
    OSObject *params[3] {OSNumber::withNumber(0ULL,32),OSNumber::withNumber(0x20ULL,32),OSData::withBytes(&zero,sizeof(zero))};
    OSObject *result=nullptr;size_t count=0;
    if(params[0] && params[1] && params[2] &&
       acpi->evaluateObject("WMDV",&result,params,3)==kIOReturnSuccess && result) {
        // Package { length, Buffer } or, depending on the WMI glue, the Buffer itself.
        OSData *buffer=OSDynamicCast(OSData,result);
        if(auto package=OSDynamicCast(OSArray,result); package && package->getCount()>=2)
            buffer=OSDynamicCast(OSData,package->getObject(1));
        if(buffer) {
            auto bytes=static_cast<const uint8_t *>(buffer->getBytesNoCopy());
            const size_t length=buffer->getLength();
            for(size_t off=0;off+3<=length && count<max;off+=3) {
                if(bytes[off]==0xFF)break;
                types[count]=bytes[off];
                rpm[count]=static_cast<uint16_t>(bytes[off+1] | (bytes[off+2]<<8));
                count++;
            }
        }
    }
    OSSafeReleaseNULL(result);
    for(auto &p:params)OSSafeReleaseNULL(p);
    return count;
}

IOService *SMCDellDDV::probe(IOService *provider,SInt32 *score) {
    if(!IOService::probe(provider,score))return nullptr;
    acpi=OSDynamicCast(IOACPIPlatformDevice,provider);
    if(!acpi || acpi->validateObject("WMDV")!=kIOReturnSuccess) {acpi=nullptr;return nullptr;}
    return this;
}

// The firmware's EC reads only report fans once the EC is fully up (ECRD set
// by _REG after AppleACPIEC registers). Early in boot the DDV query returns 0,
// so retry every PollMs for up to DiscoverTries before giving up. SMC keys must
// be added before the plugin is submitted, so submission waits for discovery.
bool SMCDellDDV::discover() {
    uint8_t types[MaxFans] {};uint16_t rpm[MaxFans] {};
    const size_t count=query(types,rpm,MaxFans);
    if(!count)return false;
    IOLog("SMCDellDDV: DDV fan query found %zu fan(s) after %u tries\n",count,tries);
    const uint16_t zeroFp=0; // fpe2 zero; no floating point in kernel code
    VirtualSMCAPI::addKey(KeyFNum,vsmcPlugin.data,VirtualSMCAPI::valueWithUint8(static_cast<uint8_t>(count),nullptr,SMC_KEY_ATTRIBUTE_CONST | SMC_KEY_ATTRIBUTE_READ));
    for(size_t i=0;i<count;i++) {
        fanRPM[i]=rpm[i];fanMax[i]=rpm[i];
        FanTypeDesc desc;
        // Linux dell-wmi-ddv types: 0x00 CPU, 0x01 GPU (dGPU), 0x02 video, others vary.
        const char *name=types[i]==0x00 ? "CPU Fan" : types[i]==0x01 ? "GPU Fan" : nullptr;
        if(name)strlcpy(desc.function,name,sizeof(desc.function));
        else snprintf(desc.function,sizeof(desc.function),"Fan %zu",i);
        VirtualSMCAPI::addKey(KeyF0ID(i),vsmcPlugin.data,VirtualSMCAPI::valueWithData(reinterpret_cast<const SMC_DATA *>(&desc),sizeof(desc),SmcKeyTypeFds,nullptr,SMC_KEY_ATTRIBUTE_CONST | SMC_KEY_ATTRIBUTE_READ));
        VirtualSMCAPI::addKey(KeyF0Ac(i),vsmcPlugin.data,VirtualSMCAPI::valueWithData(reinterpret_cast<const SMC_DATA *>(&zeroFp),sizeof(zeroFp),SmcKeyTypeFpe2,new FanAc(i),SMC_KEY_ATTRIBUTE_READ));
        VirtualSMCAPI::addKey(KeyF0Mn(i),vsmcPlugin.data,VirtualSMCAPI::valueWithData(reinterpret_cast<const SMC_DATA *>(&zeroFp),sizeof(zeroFp),SmcKeyTypeFpe2,nullptr,SMC_KEY_ATTRIBUTE_CONST | SMC_KEY_ATTRIBUTE_READ));
        VirtualSMCAPI::addKey(KeyF0Mx(i),vsmcPlugin.data,VirtualSMCAPI::valueWithData(reinterpret_cast<const SMC_DATA *>(&zeroFp),sizeof(zeroFp),SmcKeyTypeFpe2,new FanMx(i),SMC_KEY_ATTRIBUTE_READ));
    }
    fans=count;
    setProperty("FanCount",static_cast<unsigned long long>(count),32);
    vsmcNotifier=VirtualSMCAPI::registerHandler(vsmcNotificationHandler,this);
    if(!vsmcNotifier)IOLog("SMCDellDDV: VirtualSMC handler registration failed\n");
    return true;
}

void SMCDellDDV::poll() {
    if(!fans) {
        tries++;setProperty("DiscoverTries",static_cast<unsigned long long>(tries),32);
        if(!discover()) {
            if(tries<DiscoverTries) {if(timer)timer->setTimeoutMS(PollMs);}
            else IOLog("SMCDellDDV: no fans reported after %u tries, giving up\n",tries);
            return;
        }
    }
    uint8_t types[MaxFans] {};uint16_t rpm[MaxFans] {};
    const size_t count=query(types,rpm,MaxFans);
    for(size_t i=0;i<count;i++) {
        fanRPM[i]=rpm[i];
        if(rpm[i]>fanMax[i])fanMax[i]=rpm[i]; // observed maximum; firmware exposes no limit
    }
    if(timer)timer->setTimeoutMS(PollMs);
}

bool SMCDellDDV::start(IOService *provider) {
    if(!IOService::start(provider))return false;
    loop=IOWorkLoop::workLoop();
    if(loop) {
        timer=IOTimerEventSource::timerEventSource(this,[](OSObject *owner,IOTimerEventSource *) {
            static_cast<SMCDellDDV *>(owner)->poll();
        });
        if(timer && loop->addEventSource(timer)==kIOReturnSuccess)timer->setTimeoutMS(1);
        else OSSafeReleaseNULL(timer);
    }
    registerService();
    return timer!=nullptr;
}

void SMCDellDDV::stop(IOService *provider) {
    if(timer) {timer->cancelTimeout();if(loop)loop->removeEventSource(timer);OSSafeReleaseNULL(timer);}
    OSSafeReleaseNULL(loop);
    if(vsmcNotifier) {vsmcNotifier->remove();vsmcNotifier=nullptr;}
    acpi=nullptr;
    IOService::stop(provider);
}

bool SMCDellDDV::vsmcNotificationHandler(void *sensors,void *,IOService *vsmc,IONotifier *) {
    if(!sensors || !vsmc)return false;
    auto &plugin=static_cast<SMCDellDDV *>(sensors)->vsmcPlugin;
    const auto ret=vsmc->callPlatformFunction(VirtualSMCAPI::SubmitPlugin,true,sensors,&plugin,nullptr,nullptr);
    if(ret!=kIOReturnSuccess && ret!=kIOReturnUnsupported)IOLog("SMCDellDDV: plugin submission failure %x\n",ret);
    return ret==kIOReturnSuccess;
}
