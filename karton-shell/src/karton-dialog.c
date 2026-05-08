#include <gio/gio.h>
#include <glib.h>
#include <libintl.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef LOCALEDIR
#define LOCALEDIR "/usr/local/share/locale"
#endif

#define _(s) gettext(s)
#define N_(s) s

#define DIALOG_BUS_NAME "org.karton.Dialog"
#define DIALOG_OBJECT_PATH "/org/karton/Dialog"
#define DIALOG_IFACE "org.karton.Dialog1"

enum theme_mode {
    THEME_AUTO = 0,
    THEME_LIGHT = 1,
    THEME_DARK = 2,
};

static void
karton_get_config_path(char *out, size_t out_size, const char *name)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (xdg && *xdg) {
        snprintf(out, out_size, "%s/karton/%s", xdg, name);
        return;
    }

    if (!home || !home[0]) {
        out[0] = '\0';
        return;
    }
    snprintf(out, out_size, "%s/.config/karton/%s", home, name);
}

static enum theme_mode
theme_mode_parse(const char *value)
{
    if (!value || !value[0]) {
        return THEME_AUTO;
    }
    if (g_ascii_strcasecmp(value, "light") == 0) {
        return THEME_LIGHT;
    }
    if (g_ascii_strcasecmp(value, "dark") == 0) {
        return THEME_DARK;
    }
    return THEME_AUTO;
}

static enum theme_mode
load_theme_mode(void)
{
    char path[1024] = { 0 };
    karton_get_config_path(path, sizeof(path), "theme-mode");
    if (!path[0]) {
        return THEME_AUTO;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        return THEME_AUTO;
    }

    char line[64] = { 0 };
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return THEME_AUTO;
    }
    fclose(f);

    g_strstrip(line);
    return theme_mode_parse(line);
}

static bool
theme_is_dark(enum theme_mode mode)
{
    if (mode == THEME_DARK) {
        return true;
    }
    if (mode == THEME_LIGHT) {
        return false;
    }

    time_t now = time(NULL);
    struct tm tm_now = { 0 };
    if (now == (time_t)-1 || !localtime_r(&now, &tm_now)) {
        return true;
    }
    return tm_now.tm_hour < 7 || tm_now.tm_hour >= 19;
}

static bool
command_exists(const char *name)
{
    if (!name || !name[0]) {
        return false;
    }
    char *path = g_find_program_in_path(name);
    if (!path) {
        return false;
    }
    g_free(path);
    return true;
}

static bool
fallback_prompt_password(const char *title, const char *text, char **out_password)
{
    (void)title;
    (void)text;
    if (out_password) {
        *out_password = NULL;
    }
    return false;
}

static bool
fallback_prompt_question(const char *title, const char *text)
{
    (void)title;
    (void)text;
    return false;
}

static bool
ensure_dialogd_running(void)
{
    static bool attempted = false;
    if (attempted) {
        return false;
    }
    attempted = true;

    if (!command_exists("karton-dialogd")) {
        return false;
    }

    gchar *argv[] = { (gchar *)"karton-dialogd", NULL };
    gboolean ok = g_spawn_async(NULL,
                                argv,
                                NULL,
                                G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                                NULL,
                                NULL,
                                NULL,
                                NULL);
    if (!ok) {
        return false;
    }

    for (int i = 0; i < 10; i++) {
        g_usleep(100000);
        GError *err = NULL;
        GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
        if (!conn) {
            g_clear_error(&err);
            continue;
        }
        GVariant *reply = g_dbus_connection_call_sync(conn,
                                                      DIALOG_BUS_NAME,
                                                      DIALOG_OBJECT_PATH,
                                                      DIALOG_IFACE,
                                                      "GetTheme",
                                                      NULL,
                                                      G_VARIANT_TYPE("(s)"),
                                                      G_DBUS_CALL_FLAGS_NONE,
                                                      300,
                                                      NULL,
                                                      &err);
        if (reply) {
            g_variant_unref(reply);
            g_object_unref(conn);
            return true;
        }
        g_clear_error(&err);
        g_object_unref(conn);
    }

    return false;
}

static void
print_usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s --password [--title TITLE] [--text TEXT]\n"
            "       %s --question [--title TITLE] [--text TEXT]\n"
            "       %s --theme\n",
            argv0,
            argv0,
            argv0);
}

int
main(int argc, char **argv)
{
    setlocale(LC_ALL, "");
    bindtextdomain("karton-shell", LOCALEDIR);
    textdomain("karton-shell");

    bool want_password = false;
    bool want_question = false;
    bool want_theme = false;
    const char *title = _("Password");
    const char *text = _("Enter password");

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--password") == 0) {
            want_password = true;
            continue;
        }
        if (strcmp(argv[i], "--theme") == 0) {
            want_theme = true;
            continue;
        }
        if (strcmp(argv[i], "--question") == 0) {
            want_question = true;
            continue;
        }
        if (strcmp(argv[i], "--title") == 0 && i + 1 < argc) {
            title = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--text") == 0 && i + 1 < argc) {
            text = argv[++i];
            continue;
        }

        print_usage(argv[0]);
        return 2;
    }

    if (!want_password && !want_question && !want_theme) {
        print_usage(argv[0]);
        return 2;
    }

    GError *error = NULL;
    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);

    if (want_theme) {
        if (conn) {
            GVariant *reply = g_dbus_connection_call_sync(conn,
                                                          DIALOG_BUS_NAME,
                                                          DIALOG_OBJECT_PATH,
                                                          DIALOG_IFACE,
                                                          "GetTheme",
                                                          NULL,
                                                          G_VARIANT_TYPE("(s)"),
                                                          G_DBUS_CALL_FLAGS_NONE,
                                                          1000,
                                                          NULL,
                                                          &error);
            if (reply) {
                const char *theme = NULL;
                g_variant_get(reply, "(&s)", &theme);
                printf("%s\n", theme);
                g_variant_unref(reply);
                g_object_unref(conn);
                return 0;
            }
            g_clear_error(&error);
        }

        printf("%s\n", theme_is_dark(load_theme_mode()) ? "dark" : "light");
        if (conn) {
            g_object_unref(conn);
        }
        return 0;
    }

    if ((want_password || want_question) && conn) {
        GVariant *reply = g_dbus_connection_call_sync(conn,
                                                      DIALOG_BUS_NAME,
                                                      DIALOG_OBJECT_PATH,
                                                      DIALOG_IFACE,
                                                      want_question ? "PromptQuestion" : "PromptPassword",
                                                      g_variant_new("(ss)", title, text),
                                                      want_question ? G_VARIANT_TYPE("(b)") : G_VARIANT_TYPE("(bs)"),
                                                      G_DBUS_CALL_FLAGS_NONE,
                                                      -1,
                                                      NULL,
                                                      &error);
        if (reply) {
            if (want_question) {
                gboolean accepted = FALSE;
                g_variant_get(reply, "(b)", &accepted);
                g_variant_unref(reply);
                g_object_unref(conn);
                return accepted ? 0 : 1;
            } else {
                gboolean accepted = FALSE;
                const char *password = NULL;
                g_variant_get(reply, "(b&s)", &accepted, &password);
                if (accepted && password && password[0]) {
                    printf("%s\n", password);
                    g_variant_unref(reply);
                    g_object_unref(conn);
                    return 0;
                }
                g_variant_unref(reply);
                g_object_unref(conn);
                return 1;
            }
        }
        g_clear_error(&error);

        g_object_unref(conn);
        conn = NULL;
        if (ensure_dialogd_running()) {
            conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
            if (conn) {
                GVariant *reply2 = g_dbus_connection_call_sync(conn,
                                                               DIALOG_BUS_NAME,
                                                               DIALOG_OBJECT_PATH,
                                                               DIALOG_IFACE,
                                                               want_question ? "PromptQuestion" : "PromptPassword",
                                                               g_variant_new("(ss)", title, text),
                                                               want_question ? G_VARIANT_TYPE("(b)") : G_VARIANT_TYPE("(bs)"),
                                                               G_DBUS_CALL_FLAGS_NONE,
                                                               -1,
                                                               NULL,
                                                               &error);
                if (reply2) {
                    if (want_question) {
                        gboolean accepted = FALSE;
                        g_variant_get(reply2, "(b)", &accepted);
                        g_variant_unref(reply2);
                        g_object_unref(conn);
                        return accepted ? 0 : 1;
                    } else {
                        gboolean accepted = FALSE;
                        const char *password = NULL;
                        g_variant_get(reply2, "(b&s)", &accepted, &password);
                        if (accepted && password && password[0]) {
                            printf("%s\n", password);
                            g_variant_unref(reply2);
                            g_object_unref(conn);
                            return 0;
                        }
                        g_variant_unref(reply2);
                        g_object_unref(conn);
                        return 1;
                    }
                }
                g_clear_error(&error);
            }
        }
    }

    if (conn) {
        g_object_unref(conn);
    }

    if (want_question) {
        return fallback_prompt_question(title, text) ? 0 : 1;
    }

    char *password = NULL;
    bool ok = fallback_prompt_password(title, text, &password);
    if (!ok) {
        return 1;
    }

    printf("%s\n", password);
    g_free(password);
    return 0;
}
