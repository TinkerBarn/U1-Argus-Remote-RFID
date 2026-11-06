"""Exercise the actual mapped-tag handler with deterministic printer/network fakes."""
from pathlib import Path
import subprocess
import tempfile

source = Path('source/ESP32-S3/V1.8/U1_Argus_Remote_RFID_ESP32-S3_N16R8_V1_8.ino').read_text()
start = source.index('static void handleValidMappedTagPayload(')
end = source.index('// ------------------------------ Tag polling', start)
handler = source[start:end]
stubs = r'''
#include <string>
#include <cstring>
#include <cstdint>
#include <cassert>
using String=std::string;
struct OpenSpoolFields {};
struct { bool queryOk=true,hasInfo=true; uint32_t lastQueryMs=9500; } gPrinterState;
struct { uint32_t lastSentMs=0; } gWebhookState;
struct Logger { template<class... T> void printf(const char*,T...){} } Serial;
#define debugPrint(...) ((void)0)
#define debugPrintf(...) ((void)0)
uint8_t activeReaderIndex=0;
uint32_t lastTagSeenMs[2]={},lastAcceptedUidMs[2]={};
uint8_t lastAcceptedUidLen[2]={},lastAcceptedUid[2][10]={};
String lastObservedFingerprint[2],lastSentFingerprint[2];
const char* READER_LABELS[]={"left","right"};
const uint32_t PRINTER_TAG_INFO_MAX_AGE_MS=1000,WEBHOOK_RESEND_GRACE_MS=6000;
bool allowed=true,matches=false,parseOk=true,sendOk=true;
int sends=0,verifies=0,errors=0,clears=0;
String sent;
uint32_t millis(){return 10000;}
bool parseOpenSpoolFields(const String&,OpenSpoolFields&){return parseOk;}
const char* cardTypeForTagSource(const char*){return "NTAG";}
bool buildFilamentDetectPayload(const OpenSpoolFields&,const uint8_t*,uint8_t,const char*,String& p,String& f){p="FULL_METADATA_WITH_UID";f="mapped";return true;}
void pulseTagLedError(){++errors;}
void storeTagState(const OpenSpoolFields&,const String&,const uint8_t*,uint8_t,const String&,const String&,const char*){}
bool buildDesiredPrinterComparableFingerprint(const OpenSpoolFields&,const uint8_t*,uint8_t,String& s,bool){s="desired";return true;}
bool currentPrinterMatchesExpected(const String&,const String&){return matches;}
void clearPendingVerification(){++clears;}
bool allowTagSetWithFreshFilamentState(){return allowed;}
bool sendFilamentSetPayload(const String& p,const String&,const char*,bool){++sends;sent=p;return sendOk;}
void scheduleSetVerification(const String&,const String&,const String&,const String&){++verifies;}
'''
checks=r'''
int main(){
 uint8_t uid[]={1,2,3,4,5,6,7};
 handleValidMappedTagPayload("OpenSpool","valid",uid,7);
 assert(sends==1 && sent=="FULL_METADATA_WITH_UID" && verifies==1);
 // Loaded or unknown sensor: no deferred or immediate new-tag SET.
 sends=verifies=0;allowed=false;
 handleValidMappedTagPayload("OpenSpool","valid",uid,7);
 assert(sends==0 && verifies==0);
 // Matching printer metadata does not produce another SET.
 allowed=true;matches=true;
 handleValidMappedTagPayload("QIDI","valid",uid,4);
 assert(sends==0 && clears==1);
 // Duplicate resend grace is preserved.
 matches=false;lastSentFingerprint[0]="mapped";gWebhookState.lastSentMs=9900;
 handleValidMappedTagPayload("OpenSpool","valid",uid,7);assert(sends==0);
 // Rejected network request does not schedule a successful verification.
 lastSentFingerprint[0]="";sendOk=false;
 handleValidMappedTagPayload("QIDI","valid",uid,4);assert(sends==1 && verifies==0);
 // Incomplete/invalid tag content is never sent.
 sends=0;parseOk=false;
 handleValidMappedTagPayload("OpenSpool","invalid",uid,7);assert(sends==0 && errors==1);
}
'''
with tempfile.TemporaryDirectory(prefix='argus-dispatch-') as d:
    path=Path(d)/'test.cpp'; path.write_text(stubs+handler+checks)
    binary=Path(d)/'test'
    subprocess.run(['clang++','-std=c++11',str(path),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('PASS: immediate full metadata, loaded/unknown guard, matching state, duplicate grace, network failure, invalid content')
