#include <jni.h>
#include <android/log.h>
#include "And64InlineHook.hpp"
#include <link.h>
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include <fstream>
#include <iterator>
#include <sstream>
#include <iomanip>
#include <atomic>

#define TAG "LACAdminGive"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace {
using Il2CppDomain = void;
using Il2CppString = void;
using MethodInfo = void;
using FieldInfo = void;
using Il2CppClass = void;
using domain_get_fn = Il2CppDomain* (*)();
using thread_attach_fn = void* (*)(Il2CppDomain*);
using domain_get_assemblies_fn = const void** (*)(Il2CppDomain*, size_t*);
using assembly_get_image_fn = const void* (*)(const void*);
using class_from_name_fn = Il2CppClass* (*)(const void*, const char*, const char*);
using class_get_method_fn = const MethodInfo* (*)(Il2CppClass*, const char*, int);
using class_get_field_fn = FieldInfo* (*)(Il2CppClass*, const char*);
using object_get_class_fn = Il2CppClass* (*)(void*);
using field_get_value_fn = void (*)(void*, FieldInfo*, void*);
using runtime_invoke_fn = void* (*)(const MethodInfo*, void*, void**, void**);
using string_new_fn = Il2CppString* (*)(const char*);
using string_chars_fn = const uint16_t* (*)(Il2CppString*);
using string_length_fn = int32_t (*)(Il2CppString*);
using object_unbox_fn = void* (*)(void*);

static void* g_il2cpp = nullptr;
static Il2CppDomain* g_domain = nullptr;
static std::atomic<bool> g_enabled{false};
static std::atomic<bool> g_hooked{false};
static std::atomic<bool> g_installing{false};
static std::string g_il2cpp_path;
static uintptr_t g_exec_begin = 0;
static uintptr_t g_exec_end = 0;

static domain_get_fn il2cpp_domain_get = nullptr;
static thread_attach_fn il2cpp_thread_attach = nullptr;
static domain_get_assemblies_fn il2cpp_domain_get_assemblies = nullptr;
static assembly_get_image_fn il2cpp_assembly_get_image = nullptr;
static class_from_name_fn il2cpp_class_from_name = nullptr;
static class_get_method_fn il2cpp_class_get_method_from_name = nullptr;
static class_get_field_fn il2cpp_class_get_field_from_name = nullptr;
static object_get_class_fn il2cpp_object_get_class = nullptr;
static field_get_value_fn il2cpp_field_get_value = nullptr;
static runtime_invoke_fn il2cpp_runtime_invoke = nullptr;
static string_new_fn il2cpp_string_new = nullptr;
static string_chars_fn il2cpp_string_chars = nullptr;
static string_length_fn il2cpp_string_length = nullptr;
static object_unbox_fn il2cpp_object_unbox = nullptr;

static uint32_t sha_rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }
static bool sha256_file(const std::string& path, std::string& hex) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (!in.good() && !in.eof()) return false;
    const uint64_t bit_len = static_cast<uint64_t>(data.size()) * 8ULL;
    data.push_back(0x80);
    while ((data.size() % 64) != 56) data.push_back(0);
    for (int i = 7; i >= 0; --i) data.push_back(static_cast<uint8_t>(bit_len >> (i * 8)));
    static const uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };
    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    for (size_t off = 0; off < data.size(); off += 64) {
        uint32_t w[64]{};
        for (int i = 0; i < 16; ++i) {
            const size_t p = off + static_cast<size_t>(i) * 4;
            w[i] = (static_cast<uint32_t>(data[p]) << 24) | (static_cast<uint32_t>(data[p+1]) << 16) |
                   (static_cast<uint32_t>(data[p+2]) << 8) | static_cast<uint32_t>(data[p+3]);
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = sha_rotr(w[i-15],7) ^ sha_rotr(w[i-15],18) ^ (w[i-15] >> 3);
            const uint32_t s1 = sha_rotr(w[i-2],17) ^ sha_rotr(w[i-2],19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t s1=sha_rotr(e,6)^sha_rotr(e,11)^sha_rotr(e,25);
            const uint32_t ch=(e&f)^(~e&g);
            const uint32_t t1=hh+s1+ch+k[i]+w[i];
            const uint32_t s0=sha_rotr(a,2)^sha_rotr(a,13)^sha_rotr(a,22);
            const uint32_t maj=(a&b)^(a&c)^(b&c);
            const uint32_t t2=s0+maj;
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (uint32_t v : h) out << std::setw(8) << v;
    hex = out.str();
    return true;
}
static std::string locate_il2cpp_file() {
    if (!g_il2cpp_path.empty() && access(g_il2cpp_path.c_str(), R_OK) == 0) return g_il2cpp_path;
    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (std::getline(maps, line)) {
        if (line.find("libil2cpp.so") == std::string::npos) continue;
        const size_t slash = line.find('/');
        if (slash == std::string::npos) continue;
        std::string path = line.substr(slash);
        const std::string deleted = " (deleted)";
        if (path.size() >= deleted.size() && path.compare(path.size()-deleted.size(), deleted.size(), deleted) == 0)
            path.resize(path.size()-deleted.size());
        if (access(path.c_str(), R_OK) == 0) return path;
    }
    return {};
}
static bool validate_game_library() {
    const std::string path = locate_il2cpp_file();
    if (path.empty()) {
        LOGE("Cannot locate readable libil2cpp.so; refusing to install hook");
        return false;
    }
    std::string actual;
    if (!sha256_file(path, actual)) {
        LOGE("Cannot calculate SHA-256 for %s", path.c_str());
        return false;
    }
    static const char* expected = "29ca3610d9e89c2eba9f0abab11cc2adc96b00470a30136f55b3c39e10465f06";
    if (actual != expected) {
        LOGE("Game library SHA-256 mismatch; expected %s, got %s", expected, actual.c_str());
        return false;
    }
    LOGI("Game library SHA-256 verified: %s", actual.c_str());
    return true;
}


using CmdSendChatFn = void (*)(void*, void*, void*, const MethodInfo*);
static CmdSendChatFn original_CmdSendChat = nullptr;

static bool resolve_api(const char* name, void** out) {
    *out = dlsym(g_il2cpp, name);
    if (!*out) LOGE("Missing IL2CPP export: %s", name);
    return *out != nullptr;
}
#define RESOLVE(name) if (!resolve_api("il2cpp_" #name, reinterpret_cast<void**>(&il2cpp_##name))) return false

static bool load_il2cpp_api() {
    if (!g_il2cpp) g_il2cpp = dlopen("libil2cpp.so", RTLD_NOW);
    if (!g_il2cpp) {
        LOGE("dlopen libil2cpp.so failed: %s", dlerror());
        return false;
    }
    RESOLVE(domain_get);
    RESOLVE(thread_attach);
    RESOLVE(domain_get_assemblies);
    RESOLVE(assembly_get_image);
    RESOLVE(class_from_name);
    RESOLVE(class_get_method_from_name);
    RESOLVE(class_get_field_from_name);
    RESOLVE(object_get_class);
    RESOLVE(field_get_value);
    RESOLVE(runtime_invoke);
    RESOLVE(string_new);
    RESOLVE(string_chars);
    RESOLVE(string_length);
    RESOLVE(object_unbox);
    g_domain = il2cpp_domain_get();
    return g_domain != nullptr;
}

static Il2CppClass* find_class(const char* namespaze, const char* name) {
    if (!g_domain) return nullptr;
    size_t count = 0;
    const void** assemblies = il2cpp_domain_get_assemblies(g_domain, &count);
    if (!assemblies) return nullptr;
    for (size_t i = 0; i < count; ++i) {
        const void* image = il2cpp_assembly_get_image(assemblies[i]);
        if (!image) continue;
        Il2CppClass* klass = il2cpp_class_from_name(image, namespaze, name);
        if (klass) return klass;
    }
    return nullptr;
}
static const MethodInfo* method(Il2CppClass* klass, const char* name, int argc) {
    return klass ? il2cpp_class_get_method_from_name(klass, name, argc) : nullptr;
}
static void* field_ref(void* object, const char* name) {
    if (!object) return nullptr;
    Il2CppClass* klass = il2cpp_object_get_class(object);
    FieldInfo* field = klass ? il2cpp_class_get_field_from_name(klass, name) : nullptr;
    if (!field) return nullptr;
    void* value = nullptr;
    il2cpp_field_get_value(object, field, &value);
    return value;
}
static std::string to_utf8(Il2CppString* value) {
    if (!value) return {};
    const uint16_t* chars = il2cpp_string_chars(value);
    int32_t length = il2cpp_string_length(value);
    if (!chars || length <= 0) return {};
    std::string out;
    for (int32_t i = 0; i < length; ++i) {
        uint32_t c = chars[i];
        if (c <= 0x7f) out.push_back(static_cast<char>(c));
        else if (c <= 0x7ff) {
            out.push_back(static_cast<char>(0xc0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3f)));
        } else {
            out.push_back(static_cast<char>(0xe0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3f)));
        }
    }
    return out;
}
static void* invoke(const MethodInfo* m, void* object, void** args, void** exception = nullptr) {
    if (!m) return nullptr;
    void* local_exception = nullptr;
    return il2cpp_runtime_invoke(m, object, args, exception ? exception : &local_exception);
}
static bool server_active() {
    Il2CppClass* klass = find_class("Mirror", "NetworkServer");
    const MethodInfo* getter = method(klass, "get_active", 0);
    void* boxed = invoke(getter, nullptr, nullptr);
    if (!boxed) return false;
    void* unboxed = il2cpp_object_unbox(boxed);
    return unboxed && *reinterpret_cast<uint8_t*>(unboxed) != 0;
}
static std::vector<std::string> split_words(const std::string& input) {
    std::vector<std::string> words;
    std::string current;
    for (unsigned char c : input) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!current.empty()) { words.push_back(current); current.clear(); }
        } else current.push_back(static_cast<char>(c));
    }
    if (!current.empty()) words.push_back(current);
    return words;
}
static void send_reply(void* kill_feed, const std::string& message) {
    if (!kill_feed) return;
    Il2CppClass* klass = il2cpp_object_get_class(kill_feed);
    const MethodInfo* rpc = method(klass, "RpcGetChat", 3);
    if (!rpc) { LOGI("%s", message.c_str()); return; }
    Il2CppString* text = il2cpp_string_new(message.c_str());
    int32_t range = 1000000;
    uint32_t pm_id = 0;
    void* args[] = { text, &range, &pm_id };
    void* exception = nullptr;
    invoke(rpc, kill_feed, args, &exception);
    if (exception) LOGE("RpcGetChat failed while sending command result");
}
static bool is_host_or_admin(void* kill_feed) {
    if (!server_active()) return false;
    void* player_control = field_ref(kill_feed, "playerControl");
    if (!player_control) return false;
    auto* executor_device = static_cast<Il2CppString*>(field_ref(player_control, "deviceID"));
    if (!executor_device) return false;

    Il2CppClass* manager_class = find_class("", "ServerUserManager");
    void* manager = invoke(method(manager_class, "get_Instance", 0), nullptr, nullptr);
    if (!manager) return false;
    void* users = field_ref(manager, "serverUsers");
    if (!users) return false;

    Il2CppClass* list_class = il2cpp_object_get_class(users);
    const MethodInfo* count_method = method(list_class, "get_Count", 0);
    const MethodInfo* item_method = method(list_class, "get_Item", 1);
    if (!count_method || !item_method) return false;
    void* count_box = invoke(count_method, users, nullptr);
    void* count_ptr = count_box ? il2cpp_object_unbox(count_box) : nullptr;
    if (!count_ptr) return false;
    int32_t count = *reinterpret_cast<int32_t*>(count_ptr);
    if (count < 0 || count > 10000) return false;

    Il2CppClass* user_class = find_class("", "ServerUser");
    FieldInfo* matter_field = user_class ? il2cpp_class_get_field_from_name(user_class, "matter") : nullptr;
    FieldInfo* device_field = user_class ? il2cpp_class_get_field_from_name(user_class, "deviceID") : nullptr;
    if (!matter_field || !device_field) return false;

    const std::string executor_id = to_utf8(executor_device);
    for (int32_t i = 0; i < count; ++i) {
        void* args[] = { &i };
        void* user = invoke(item_method, users, args);
        if (!user) continue;
        uint8_t state = 0xff; // UserState has an 8-bit underlying type in LAC 1.9.7.
        void* device = nullptr;
        il2cpp_field_get_value(user, matter_field, &state);
        il2cpp_field_get_value(user, device_field, &device);
        if ((state == 1 || state == 2) && device &&
            to_utf8(static_cast<Il2CppString*>(device)) == executor_id) return true;
    }
    return false;
}
static bool handle_admin_give(void* kill_feed, Il2CppString* chat) {
    const auto words = split_words(to_utf8(chat));
    if (words.size() < 2 || words[0] != "/admin" || words[1] != "give") return false;

    if (!server_active()) {
        send_reply(kill_feed, "<color=red>[ADMIN]</color> هذا الأمر متاح على السيرفر فقط.");
        return true;
    }
    if (!is_host_or_admin(kill_feed)) {
        send_reply(kill_feed, "<color=red>[ADMIN]</color> ليس لديك صلاحية لتنفيذ هذا الأمر.");
        return true;
    }
    if (words.size() != 5) {
        send_reply(kill_feed, "<color=yellow>[ADMIN]</color> الاستخدام: /admin give <playerID> <itemID> <quantity>");
        return true;
    }

    const std::string& target_id = words[2];
    const std::string& item_id = words[3];
    const std::string& quantity_text = words[4];
    if (target_id.empty() || item_id.empty() || item_id.size() > 80 ||
        item_id.find_first_of("\r\n") != std::string::npos) {
        send_reply(kill_feed, "<color=red>[ADMIN]</color> معرّف اللاعب أو العنصر غير صالح.");
        return true;
    }

    char* end = nullptr;
    long long quantity = std::strtoll(quantity_text.c_str(), &end, 10);
    if (!end || *end != '\0' || quantity <= 0 || quantity > 1000000000LL) {
        send_reply(kill_feed, "<color=red>[ADMIN]</color> الكمية يجب أن تكون رقمًا موجبًا.");
        return true;
    }

    Il2CppClass* players_class = find_class("", "PlayersManager");
    const MethodInfo* get_player = method(players_class, "GetPlayer", 1);
    if (!get_player) {
        send_reply(kill_feed, "<color=red>[ADMIN]</color> تعذّر الوصول إلى نظام اللاعبين.");
        return true;
    }
    Il2CppString* target_string = il2cpp_string_new(target_id.c_str());
    void* get_args[] = { target_string };
    void* target = invoke(get_player, nullptr, get_args);
    if (!target) {
        send_reply(kill_feed, "<color=red>[ADMIN]</color> اللاعب غير موجود أو غير متصل.");
        return true;
    }

    Il2CppClass* player_class = il2cpp_object_get_class(target);
    const MethodInfo* add = method(player_class, "InventoryAdd", 2);
    const MethodInfo* sync = method(player_class, "RpcUpdateInventory", 1);
    if (!add || !sync) {
        send_reply(kill_feed, "<color=red>[ADMIN]</color> دوال المخزون غير متاحة في هذا الإصدار.");
        return true;
    }

    const std::string before = to_utf8(static_cast<Il2CppString*>(field_ref(target, "inventory")));
    Il2CppString* item = il2cpp_string_new(item_id.c_str());
    Il2CppString* amount = il2cpp_string_new(quantity_text.c_str());
    void* add_args[] = { item, amount };
    void* exception = nullptr;
    invoke(add, target, add_args, &exception);
    if (exception) {
        send_reply(kill_feed, "<color=red>[ADMIN]</color> رفضت اللعبة إضافة هذا العنصر.");
        return true;
    }

    auto* updated_inventory = static_cast<Il2CppString*>(field_ref(target, "inventory"));
    const std::string after = to_utf8(updated_inventory);
    if (!updated_inventory || after.empty() || after == before) {
        send_reply(kill_feed, "<color=red>[ADMIN]</color> لم يتغير المخزون؛ راجع معرّف العنصر والكمية.");
        return true;
    }

    void* sync_args[] = { updated_inventory };
    exception = nullptr;
    invoke(sync, target, sync_args, &exception);
    if (exception) {
        send_reply(kill_feed, "<color=red>[ADMIN]</color> أُضيف العنصر على السيرفر لكن فشلت مزامنة المخزون.");
        return true;
    }
    send_reply(kill_feed, "<color=green>[ADMIN]</color> تمت إضافة " + quantity_text +
                " من " + item_id + " للاعب " + target_id + ".");
    LOGI("Granted %s x%s to player %s", item_id.c_str(), quantity_text.c_str(), target_id.c_str());
    return true;
}
static void hooked_CmdSendChat(void* self, void* prefix, void* chat, const MethodInfo* method_info) {
    static thread_local bool attached = false;
    if (!attached && g_domain && il2cpp_thread_attach) {
        il2cpp_thread_attach(g_domain);
        attached = true;
    }
    if (g_enabled.load(std::memory_order_acquire) && chat &&
        handle_admin_give(self, static_cast<Il2CppString*>(chat))) return;
    if (original_CmdSendChat) original_CmdSendChat(self, prefix, chat, method_info);
}
static int find_il2cpp_base(struct dl_phdr_info* info, size_t, void* data) {
    if (info->dlpi_name && std::strstr(info->dlpi_name, "libil2cpp.so")) {
        *static_cast<uintptr_t*>(data) = static_cast<uintptr_t>(info->dlpi_addr);
        g_il2cpp_path = info->dlpi_name;
        g_exec_begin = 0;
        g_exec_end = 0;
        for (ElfW(Half) i = 0; i < info->dlpi_phnum; ++i) {
            const ElfW(Phdr)& ph = info->dlpi_phdr[i];
            if (ph.p_type == PT_LOAD && (ph.p_flags & PF_X)) {
                g_exec_begin = static_cast<uintptr_t>(ph.p_vaddr);
                g_exec_end = g_exec_begin + static_cast<uintptr_t>(ph.p_memsz);
                break;
            }
        }
        return 1;
    }
    return 0;
}
static void* install_worker(void*) {
    for (int attempt = 0; attempt < 300 && !g_hooked.load(); ++attempt) {
        if (!g_il2cpp && !load_il2cpp_api()) { usleep(100000); continue; }
        uintptr_t base = 0;
        dl_iterate_phdr(find_il2cpp_base, &base);
        if (!base || !g_domain) { usleep(100000); continue; }
        if (!validate_game_library()) { g_installing.store(false); return nullptr; }
        const uintptr_t hook_rva = 0x1080A54;
        if (g_exec_begin == 0 || hook_rva < g_exec_begin || hook_rva + sizeof(uint32_t) > g_exec_end) {
            LOGE("Hook RVA is outside the executable PT_LOAD segment");
            g_installing.store(false);
            return nullptr;
        }
        void* target = reinterpret_cast<void*>(base + hook_rva);
        const uint32_t expected_prologue = 0xD10703FF;
        if (*reinterpret_cast<const uint32_t*>(target) != expected_prologue) {
            LOGE("Version guard failed at UserCode_CmdSendChat RVA 0x1080A54; hook not installed");
            g_installing.store(false);
            return nullptr;
        }
        A64HookFunction(target, reinterpret_cast<void*>(hooked_CmdSendChat),
                        reinterpret_cast<void**>(&original_CmdSendChat));
        if (original_CmdSendChat) {
            g_enabled.store(true, std::memory_order_release);
            g_hooked.store(true, std::memory_order_release);
            LOGI("OK: /admin give hook installed (LAC 1.9.7 arm64)");
            g_installing.store(false);
            return nullptr;
        }
        LOGE("A64HookFunction did not return an original trampoline");
        g_installing.store(false);
        return nullptr;
    }
    LOGE("Timed out waiting for IL2CPP initialization");
    g_installing.store(false);
    return nullptr;
}
} // namespace

extern "C" JNIEXPORT void JNICALL Java_lacadmin_AdminGivePlugin_nativeStart(JNIEnv*, jclass);
extern "C" JNIEXPORT void JNICALL Java_lacadmin_AdminGivePlugin_nativeStop(JNIEnv*, jclass);

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* env = nullptr;
    if (!vm || vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK || !env) {
        LOGE("JNI_OnLoad: could not obtain JNIEnv");
        return JNI_ERR;
    }
    jclass plugin = env->FindClass("lacadmin/AdminGivePlugin");
    if (!plugin) {
        LOGE("JNI_OnLoad: cannot find lacadmin/AdminGivePlugin in the plugin ClassLoader");
        if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
        return JNI_ERR;
    }
    JNINativeMethod methods[] = {
        { const_cast<char*>("nativeStart"), const_cast<char*>("()V"), reinterpret_cast<void*>(Java_lacadmin_AdminGivePlugin_nativeStart) },
        { const_cast<char*>("nativeStop"), const_cast<char*>("()V"), reinterpret_cast<void*>(Java_lacadmin_AdminGivePlugin_nativeStop) }
    };
    const jint result = env->RegisterNatives(plugin, methods, sizeof(methods) / sizeof(methods[0]));
    env->DeleteLocalRef(plugin);
    if (result != JNI_OK) {
        LOGE("JNI_OnLoad: RegisterNatives failed (%d)", result);
        if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
        return JNI_ERR;
    }
    LOGI("JNI_OnLoad: nativeStart/nativeStop explicitly registered");
    return JNI_VERSION_1_6;
}
extern "C" JNIEXPORT void JNICALL
Java_lacadmin_AdminGivePlugin_nativeStart(JNIEnv*, jclass) {
    if (g_hooked.load(std::memory_order_acquire)) {
        g_enabled.store(true, std::memory_order_release);
        LOGI("AdminGive hook already installed; re-enabled");
        return;
    }
    bool expected = false;
    if (!g_installing.compare_exchange_strong(expected, true)) {
        LOGI("AdminGive installation is already running");
        return;
    }
    pthread_t worker;
    if (pthread_create(&worker, nullptr, install_worker, nullptr) == 0) {
        pthread_detach(worker);
        LOGI("AdminGive hook worker started");
    } else {
        g_installing.store(false);
        LOGE("Could not start hook worker");
    }
}
extern "C" JNIEXPORT void JNICALL
Java_lacadmin_AdminGivePlugin_nativeStop(JNIEnv*, jclass) {
    g_enabled.store(false, std::memory_order_release);
    LOGI("AdminGive disabled; hook is inert until re-enabled or process restart");
}
