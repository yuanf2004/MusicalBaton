#ifndef TEST_NAME_SETTINGS_H
#define TEST_NAME_SETTINGS_H
#include <stddef.h>
#include <sys/types.h>
typedef ssize_t (*settings_read_cb)(void *, void *, size_t);
typedef int (*test_setting_setter)(const char *, size_t, settings_read_cb, void *);
#define SETTINGS_STATIC_HANDLER_DEFINE(id, path, get, setter, commit, export) \
    test_setting_setter test_name_setter = setter
int settings_subsys_init(void);
int settings_load_subtree(const char *path);
int settings_save_one(const char *key, const void *value, size_t len);
#endif
