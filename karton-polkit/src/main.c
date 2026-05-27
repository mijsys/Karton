#define POLKIT_AGENT_I_KNOW_API_IS_SUBJECT_TO_CHANGE 1
#include <gtk/gtk.h>
#include <polkitagent/polkitagent.h>
#include <pwd.h>
#include <glib/gi18n.h>
#include <locale.h>

// Zabezpieczenie przed exploitami PID: W systemach X11 polegano na PID subject, 
// natomiast nowa implementacja D-BUS nakłada weryfikację subjectu polkita ściśle z procesem ubiegającym się, co zmniejsza ryzyko hijacking'u PID.

#define KARTON_TYPE_POLKIT_LISTENER karton_polkit_listener_get_type()
G_DECLARE_FINAL_TYPE(KartonPolkitListener, karton_polkit_listener, KARTON, POLKIT_LISTENER, PolkitAgentListener)

struct _KartonPolkitListener {
    PolkitAgentListener parent_instance;
    GtkApplication *app;
    
    PolkitAgentSession *session;
    GtkWidget *window;
    GtkWidget *password_entry;
    GtkWidget *msg_label;
    GtkWidget *error_label;
    
    GTask *task;
};

G_DEFINE_TYPE(KartonPolkitListener, karton_polkit_listener, POLKIT_AGENT_TYPE_LISTENER)

static void try_authenticate(KartonPolkitListener *self) {
    const char *password = gtk_editable_get_text(GTK_EDITABLE(self->password_entry));
    if (self->session) {
        polkit_agent_session_response(self->session, password);
    }
    // BEZPIECZEŃSTWO: Natychmiast po wysłaniu ukrywamy, nadpisujemy i czyścimy pole tekstowe 
    // by hasło nie wisiało w buforze pamięci GTK4 / Waylanda
    gtk_editable_set_text(GTK_EDITABLE(self->password_entry), "");
    gtk_widget_set_sensitive(self->password_entry, FALSE);
}

static void on_password_submitted(GtkEntry *entry, gpointer user_data) {
    (void)entry;
    try_authenticate(KARTON_POLKIT_LISTENER(user_data));
}

static void on_auth_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    try_authenticate(KARTON_POLKIT_LISTENER(user_data));
}

static void on_cancel_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    KartonPolkitListener *self = KARTON_POLKIT_LISTENER(user_data);
    if (self->session) {
        polkit_agent_session_cancel(self->session);
    }
}

static void on_session_request(PolkitAgentSession *session, gchar *request, gboolean echo_on, gpointer user_data) {
    (void)session; (void)request; (void)echo_on;
    KartonPolkitListener *self = KARTON_POLKIT_LISTENER(user_data);
    gtk_widget_set_sensitive(self->password_entry, TRUE);
    gtk_widget_grab_focus(self->password_entry);
    gtk_label_set_text(GTK_LABEL(self->error_label), "");
}

static void on_session_show_error(PolkitAgentSession *session, gchar *text, gpointer user_data) {
    (void)session;
    KartonPolkitListener *self = KARTON_POLKIT_LISTENER(user_data);
    gtk_label_set_text(GTK_LABEL(self->error_label), text);
    gtk_widget_add_css_class(self->error_label, "error");
    gtk_widget_set_sensitive(self->password_entry, TRUE);
    gtk_widget_grab_focus(self->password_entry);
}

static void on_session_show_info(PolkitAgentSession *session, gchar *text, gpointer user_data) {
    (void)session;
    KartonPolkitListener *self = KARTON_POLKIT_LISTENER(user_data);
    gtk_label_set_text(GTK_LABEL(self->msg_label), text);
}

static void on_session_completed(PolkitAgentSession *session, gboolean gained_authorization, gpointer user_data) {
    (void)session;
    KartonPolkitListener *self = KARTON_POLKIT_LISTENER(user_data);
    
    if (self->window) {
        gtk_window_destroy(GTK_WINDOW(self->window));
        self->window = NULL;
    }

    if (self->task) {
        if (!gained_authorization) {
            g_task_return_new_error(self->task, POLKIT_ERROR, POLKIT_ERROR_NOT_AUTHORIZED, _("Authorization failed"));
        } else {
            g_task_return_boolean(self->task, TRUE);
        }
        g_clear_object(&self->task);
    }
    g_clear_object(&self->session);
}

static void karton_polkit_listener_initiate_authentication (
    PolkitAgentListener  *listener,
    const gchar          *action_id,
    const gchar          *message,
    const gchar          *icon_name,
    PolkitDetails        *details,
    const gchar          *cookie,
    GList                *identities,
    GCancellable         *cancellable,
    GAsyncReadyCallback   callback,
    gpointer              user_data)
{
    (void)action_id; (void)icon_name; (void)details;
    KartonPolkitListener *self = KARTON_POLKIT_LISTENER(listener);
    
    if (self->task) {
        g_task_return_new_error(self->task, POLKIT_ERROR, POLKIT_ERROR_FAILED, _("Aborted by new session"));
        g_clear_object(&self->task);
    }
    if (self->window) {
        gtk_window_destroy(GTK_WINDOW(self->window));
        self->window = NULL;
    }

    self->task = g_task_new(listener, cancellable, callback, user_data);

    PolkitIdentity *identity = NULL;
    if (identities != NULL) {
        identity = POLKIT_IDENTITY(identities->data);
    }

    if (!identity) {
        g_task_return_new_error(self->task, POLKIT_ERROR, POLKIT_ERROR_FAILED, _("No user for authorization"));
        g_clear_object(&self->task);
        return;
    }

    self->session = polkit_agent_session_new(identity, cookie);
    g_signal_connect(self->session, "request", G_CALLBACK(on_session_request), self);
    g_signal_connect(self->session, "show-error", G_CALLBACK(on_session_show_error), self);
    g_signal_connect(self->session, "show-info", G_CALLBACK(on_session_show_info), self);
    g_signal_connect(self->session, "completed", G_CALLBACK(on_session_completed), self);

    self->window = gtk_application_window_new(self->app);
    gtk_window_set_title(GTK_WINDOW(self->window), _("Authentication - Tektura"));
    gtk_window_set_default_size(GTK_WINDOW(self->window), 400, -1);
    
    // BEZPIECZEŃSTWO: Wymuszenie ekskluzywności. Modal zapobiega zniknięciu pod innymi onkami
    gtk_window_set_modal(GTK_WINDOW(self->window), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(self->window), FALSE);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(box, 32);
    gtk_widget_set_margin_end(box, 32);
    gtk_widget_set_margin_top(box, 32);
    gtk_widget_set_margin_bottom(box, 32);
    gtk_window_set_child(GTK_WINDOW(self->window), box);

    GtkWidget *title_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    GtkWidget *icon = gtk_image_new_from_icon_name("dialog-password");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 64);
    
    GtkWidget *text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gchar *markup = g_strdup_printf("<span size='large' weight='bold'>%s</span>", _("Authentication Required"));
    GtkWidget *title_label = gtk_label_new(markup);
    g_free(markup);
    
    gtk_label_set_use_markup(GTK_LABEL(title_label), TRUE);
    gtk_widget_set_halign(title_label, GTK_ALIGN_START);
    
    self->msg_label = gtk_label_new(message);
    gtk_label_set_wrap(GTK_LABEL(self->msg_label), TRUE);
    gtk_widget_set_halign(self->msg_label, GTK_ALIGN_START);
    
    gtk_box_append(GTK_BOX(text_box), title_label);
    gtk_box_append(GTK_BOX(text_box), self->msg_label);
    gtk_box_append(GTK_BOX(title_box), icon);
    gtk_box_append(GTK_BOX(title_box), text_box);
    gtk_box_append(GTK_BOX(box), title_box);

    self->error_label = gtk_label_new("");
    gtk_widget_set_halign(self->error_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), self->error_label);

    self->password_entry = gtk_password_entry_new();
    gtk_password_entry_set_show_peek_icon(GTK_PASSWORD_ENTRY(self->password_entry), TRUE);
    gtk_widget_set_sensitive(self->password_entry, FALSE);
    g_signal_connect(self->password_entry, "activate", G_CALLBACK(on_password_submitted), self);
    gtk_box_append(GTK_BOX(box), self->password_entry);

    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_halign(btn_box, GTK_ALIGN_END);
    
    GtkWidget *btn_cancel = gtk_button_new_with_label(_("Cancel"));
    g_signal_connect(btn_cancel, "clicked", G_CALLBACK(on_cancel_clicked), self);
    
    GtkWidget *btn_auth = gtk_button_new_with_label(_("Authenticate"));
    gtk_widget_add_css_class(btn_auth, "suggested-action");
    g_signal_connect(btn_auth, "clicked", G_CALLBACK(on_auth_clicked), self);

    gtk_box_append(GTK_BOX(btn_box), btn_cancel);
    gtk_box_append(GTK_BOX(btn_box), btn_auth);
    gtk_box_append(GTK_BOX(box), btn_box);

    gtk_window_present(GTK_WINDOW(self->window));
    polkit_agent_session_initiate(self->session);
}

static gboolean karton_polkit_listener_initiate_authentication_finish (
    PolkitAgentListener  *listener,
    GAsyncResult         *res,
    GError              **error)
{
    (void)listener;
    return g_task_propagate_boolean(G_TASK(res), error);
}

static void karton_polkit_listener_class_init(KartonPolkitListenerClass *klass) {
    PolkitAgentListenerClass *listener_class = POLKIT_AGENT_LISTENER_CLASS(klass);
    listener_class->initiate_authentication = karton_polkit_listener_initiate_authentication;
    listener_class->initiate_authentication_finish = karton_polkit_listener_initiate_authentication_finish;
}

static void karton_polkit_listener_init(KartonPolkitListener *self) { (void)self; }

static PolkitAgentListener *karton_polkit_listener_register_new(GtkApplication *app) {
    KartonPolkitListener *self = g_object_new(KARTON_TYPE_POLKIT_LISTENER, NULL);
    self->app = app;
    PolkitSubject *subject = polkit_unix_session_new_for_process_sync(getpid(), NULL, NULL);

    GError *error = NULL;
    gpointer registration_handle = polkit_agent_listener_register(
        POLKIT_AGENT_LISTENER(self),
        POLKIT_AGENT_REGISTER_FLAGS_NONE,
        subject, NULL, NULL, &error
    );
    (void)registration_handle;

    if (subject) g_object_unref(subject);

    if (error != NULL) {
        g_printerr("Fatal Error Polkit: %s\n", error->message);
        g_error_free(error);
        g_object_unref(self);
        return NULL;
    }
    return POLKIT_AGENT_LISTENER(self);
}

static PolkitAgentListener *global_listener = NULL;

static void activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;
    g_application_hold(G_APPLICATION(app));
    if (!global_listener) {
        global_listener = karton_polkit_listener_register_new(app);
    }
}

int main(int argc, char **argv) {
    setlocale(LC_ALL, "");
    bindtextdomain("karton-polkit", "/usr/share/locale");
    textdomain("karton-polkit");

    GtkApplication *app = gtk_application_new("io.karton.Polkit", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    
    if (global_listener) {
        g_object_unref(global_listener);
    }
    g_object_unref(app);
    return status;
}
