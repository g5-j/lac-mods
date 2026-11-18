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
        void* target = reinterpret_cast<void*>(base + 0x1080A54);
        const uint32_t expected_prologue = 0xD10703FF;
        if (*reinterpret_cast<const uint32_t*>(target) != expected_prologue) {
            LOGE("Version guard failed at CmdSendChat RVA 0x1080A54; hook not installed");
            return nullptr;
        }
        A64HookFunction(target, reinterpret_cast<void*>(hooked_CmdSendChat),
                        reinterpret_cast<void**>(&original_CmdSendChat));
        if (original_CmdSendChat) {
            g_enabled.store(true, std::memory_order_release);
            g_hooked.store(true, std::memory_order_release);
            LOGI("OK: /admin give hook installed (LAC 1.9.7 arm64)");
            return nullptr;
        }
        LOGE("A64HookFunction did not return an original trampoline");
        return nullptr;
    }
    LOGE("Timed out waiting for IL2CPP initialization");
    return nullptr;
}
} // namespace

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM*, void*) {
    return JNI_VERSION_1_6;
}
extern "C" JNIEXPORT void JNICALL
Java_lacadmin_AdminGivePlugin_nativeStart(JNIEnv*, jclass) {
    if (g_hooked.load()) { g_enabled.store(true); return; }
    pthread_t worker;
    if (pthread_create(&worker, nullptr, install_worker, nullptr) == 0) pthread_detach(worker);
    else LOGE("Could not start hook worker");
}
extern "C" JNIEXPORT void JNICALL
Java_lacadmin_AdminGivePlugin_nativeStop(JNIEnv*, jclass) {
    g_enabled.store(false, std::memory_order_release);
    LOGI("AdminGive disabled; process restart fully unloads native hook");
}
