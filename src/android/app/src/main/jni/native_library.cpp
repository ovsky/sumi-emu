#include "core/crypto/key_manager.h"
#include "core/hle/service/am/am.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/content_archive.h"
#include "core/system.h"

extern "C" {

JNIEXPORT jboolean JNICALL Java_org_sumi_sumi_1emu_NativeLibrary_isFirmwareAvailable(
    JNIEnv* env, jobject obj) {
    return Core::Crypto::KeyManager::Instance().IsFirmwareAvailable();
}

JNIEXPORT jboolean JNICALL Java_org_sumi_sumi_1emu_NativeLibrary_checkFirmwarePresence(
    JNIEnv* env, jobject obj) {
    constexpr u64 MiiEditId = 0x0100000000001009; // Mii Edit applet ID
    constexpr u64 QLaunchId = 0x0100000000001000; // Home Menu applet ID

    auto& system = Core::System::GetInstance();
    auto bis_system = system.GetFileSystemController().GetSystemNANDContents();
    if (!bis_system) {
        return false;
    }

    auto mii_applet_nca = bis_system->GetEntry(MiiEditId, FileSys::ContentRecordType::Program);
    auto qlaunch_nca = bis_system->GetEntry(QLaunchId, FileSys::ContentRecordType::Program);

    return (mii_applet_nca != nullptr && qlaunch_nca != nullptr);
}

} // extern "C"