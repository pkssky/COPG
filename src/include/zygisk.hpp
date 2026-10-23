// This is the public API for Zygisk modules.
// DO NOT MODIFY ANY CODE IN THIS HEADER.
//
// Copy of the official Zygisk module API (ZYGISK_API_VERSION 1), taken from
// RikkaApps/Sui (module/src/main/cpp/include/zygisk.hpp). Implemented by
// Magisk's built-in Zygisk, Zygisk Next, ReZygisk and NeoZygisk.
//
// Two documented deviations from the upstream copy, both forced by modern
// clang (NDK r29 verified):
// 1. The entry-point declarations place their attributes AFTER extern "C".
//    Upstream puts them on their own line BEFORE extern "C", which newer
//    clang rejects with "an attribute list cannot appear here". Same
//    semantics, different (valid) placement.
// 2. ModuleBase's default no-op bodies cast unused parameters to void so
//    -Wunused-parameter (part of -Wextra) stays quiet in every TU.
// 3. Api's out-of-line member definitions below are marked inline. Upstream
//    omits it (their module compiles a single TU that includes this header);
//    this engine has four such TUs, and without inline the linker rejects
//    the duplicate symbol definitions.

#pragma once

#include <jni.h>

#define ZYGISK_API_VERSION 1

namespace zygisk {

    struct Api;
    struct AppSpecializeArgs;
    struct ServerSpecializeArgs;

    class ModuleBase {
    public:

        // This function is called when the module is loaded into the target process.
        // A Zygisk API handle will be sent as an argument; call utility functions or interface
        // with Zygisk through this handle.
        virtual void onLoad(Api *api, JNIEnv *env) { (void) api; (void) env; }

        // This function is called before the app process is specialized.
        // At this point, the process just got forked from zygote, but no app specific specialization
        // is applied. This means that the process does not have any sandbox restrictions and
        // still runs with the same privilege of zygote.
        virtual void preAppSpecialize(AppSpecializeArgs *args) { (void) args; }

        // This function is called after the app process is specialized.
        // At this point, the process has all sandbox restrictions enabled for this application.
        // This means that this function runs as the same privilege of the app's own code.
        virtual void postAppSpecialize(const AppSpecializeArgs *args) { (void) args; }

        // This function is called before the system server process is specialized.
        virtual void preServerSpecialize(ServerSpecializeArgs *args) { (void) args; }

        // This function is called after the system server process is specialized.
        // At this point, the process runs with the privilege of system_server.
        virtual void postServerSpecialize(const ServerSpecializeArgs *args) { (void) args; }
    };

    struct AppSpecializeArgs {
        jint &uid;
        jint &gid;
        jintArray &gids;
        jint &runtime_flags;
        jint &mount_external;
        jstring &se_info;
        jstring &nice_name;
        jstring &instruction_set;
        jstring &app_data_dir;

        jboolean *const is_child_zygote;
        jboolean *const is_top_app;
        jobjectArray *const pkg_data_info_list;
        jobjectArray *const whitelisted_data_info_list;
        jboolean *const mount_data_dirs;
        jboolean *const mount_storage_dirs;

        AppSpecializeArgs() = delete;
    };

    struct ServerSpecializeArgs {
        jint &uid;
        jint &gid;
        jintArray &gids;
        jint &runtime_flags;
        jlong &permitted_capabilities;
        jlong &effective_capabilities;

        ServerSpecializeArgs() = delete;
    };

    namespace internal {
        struct api_table;
        template <class T> void entry_impl(api_table *, JNIEnv *);
    }

    enum Option : int {
        FORCE_DENYLIST_UNMOUNT = 0,
        DLCLOSE_MODULE_LIBRARY = 1,
    };

    struct Api {

        int connectCompanion();

        void setOption(Option opt);

        void hookJniNativeMethods(JNIEnv *env, const char *className, JNINativeMethod *methods, int numMethods);

        // For ELFs loaded in memory matching `regex`, replace function `symbol` with `newFunc`.
        void pltHookRegister(const char *regex, const char *symbol, void *newFunc, void **oldFunc);

        void pltHookExclude(const char *regex, const char *symbol);

        bool pltHookCommit();

    private:
        internal::api_table *impl;
        template <class T> friend void internal::entry_impl(internal::api_table *, JNIEnv *);
    };

#define REGISTER_ZYGISK_MODULE(clazz) \
void zygisk_module_entry(zygisk::internal::api_table *table, JNIEnv *env) { \
    zygisk::internal::entry_impl<clazz>(table, env);                        \
}

#define REGISTER_ZYGISK_COMPANION(func) \
void zygisk_companion_entry(int client) { func(client); }

    namespace internal {

        struct module_abi {
            long api_version;
            ModuleBase *_this;

            void (*preAppSpecialize)(ModuleBase *, AppSpecializeArgs *);
            void (*postAppSpecialize)(ModuleBase *, const AppSpecializeArgs *);
            void (*preServerSpecialize)(ModuleBase *, ServerSpecializeArgs *);
            void (*postServerSpecialize)(ModuleBase *, const ServerSpecializeArgs *);

            module_abi(ModuleBase *module) : api_version(ZYGISK_API_VERSION), _this(module) {
                preAppSpecialize = [](auto self, auto args) { self->preAppSpecialize(args); };
                postAppSpecialize = [](auto self, auto args) { self->postAppSpecialize(args); };
                preServerSpecialize = [](auto self, auto args) { self->preServerSpecialize(args); };
                postServerSpecialize = [](auto self, auto args) { self->postServerSpecialize(args); };
            }
        };

        struct api_table {
            void *_this;
            bool (*registerModule)(api_table *, module_abi *);

            void (*hookJniNativeMethods)(JNIEnv *, const char *, JNINativeMethod *, int);
            void (*pltHookRegister)(const char *, const char *, void *, void **);
            void (*pltHookExclude)(const char *, const char *);
            bool (*pltHookCommit)();

            int  (*connectCompanion)(void *);
            void (*setOption)(void *, Option);
        };

        template <class T>
        void entry_impl(api_table *table, JNIEnv *env) {
            ModuleBase *module = new T();
            if (!table->registerModule(table, new module_abi(module)))
                return;
            auto api = new Api();
            api->impl = table;
            module->onLoad(api, env);
        }

    }

    inline int Api::connectCompanion() {
        return impl->connectCompanion(impl->_this);
    }
    inline void Api::setOption(Option opt) {
        impl->setOption(impl->_this, opt);
    }
    inline void Api::hookJniNativeMethods(JNIEnv *env, const char *className, JNINativeMethod *methods, int numMethods) {
        impl->hookJniNativeMethods(env, className, methods, numMethods);
    }
    inline void Api::pltHookRegister(const char *regex, const char *symbol, void *newFunc, void **oldFunc) {
        impl->pltHookRegister(regex, symbol, newFunc, oldFunc);
    }
    inline void Api::pltHookExclude(const char *regex, const char *symbol) {
        impl->pltHookExclude(regex, symbol);
    }
    inline bool Api::pltHookCommit() {
        return impl->pltHookCommit();
    }

}

// Attributes must follow extern "C": clang (NDK r29+) rejects an attribute
// list on its own line before a linkage-specification. The visibility/used
// attributes still apply to the definition produced by REGISTER_ZYGISK_MODULE.
extern "C" [[gnu::visibility("default")]] [[gnu::used]]
void zygisk_module_entry(zygisk::internal::api_table *, JNIEnv *);

extern "C" [[gnu::visibility("default")]] [[gnu::used]]
void zygisk_companion_entry(int);