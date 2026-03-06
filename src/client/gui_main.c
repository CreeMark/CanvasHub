#include <gtk/gtk.h>
#include <cairo.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include "cJSON.h"
#include "canvas.h"
#include "protocol.h"
#include "network.h"

// External declarations for Undo/Redo
const draw_cmd_t *undo_redo_undo(void);
const draw_cmd_t *undo_redo_redo(void);
void undo_redo_push(const draw_cmd_t *cmd);
void undo_redo_init(int max_steps);
void undo_redo_iterate(void (*cb)(const draw_cmd_t *, void *), void *user_data);
char *undo_redo_serialize(void);
void undo_redo_deserialize(const char *json);

typedef enum {
    APP_PHASE_LOGIN,
    APP_PHASE_LOBBY,
    APP_PHASE_CANVAS
} AppPhase;

typedef struct {
    GtkApplication *gtk_app;
    GtkWidget *window;
    GtkWidget *main_container; // Holds the current view

    // Canvas Widgets
    GtkWidget *drawing_area;
    cairo_surface_t *surface;
    GtkWidget *chat_entry;
    GtkTextBuffer *chat_buffer;

    // Login Widgets
    GtkWidget *login_entry_user;
    GtkWidget *login_entry_pass;
    GtkWidget *login_entry_email; // Added email widget
    GtkWidget *login_status_label;
    GtkWidget *login_btn_box; // To toggle visibility
    GtkWidget *register_container; // For toggle

    // Lobby Widgets
    GtkListStore *room_list_store;
    GtkWidget *room_tree_view;
    GtkWidget *create_room_entry;

    // State
    AppPhase phase;
    struct {
        double x, y;
        int is_drawing;
        int tool_type;
        double line_width;
        struct { double r, g, b; } color;
    } state;

    net_client_t *client;
    char username[64];
    uint32_t user_id;
    uint32_t current_room_id;
} AppWidgets;

// Forward declarations
static void build_login_ui(AppWidgets *app);
static void build_lobby_ui(AppWidgets *app);
static void build_canvas_ui(AppWidgets *app);
static void repaint_all(AppWidgets *app);

// --- Network Callbacks ---

static void on_net_connect(const char *msg, size_t len, void *user_data) {
    (void)msg; (void)len;
    AppWidgets *app = (AppWidgets *)user_data;
    g_print("Connected to server\n");
    // Update label to show connection status
    if (app->login_status_label) {
        gtk_label_set_text(GTK_LABEL(app->login_status_label), "Connected to Server");
    }
}

static void on_net_disconnect(const char *msg, size_t len, void *user_data) {
    (void)msg; (void)len;
    AppWidgets *app = (AppWidgets *)user_data;
    g_print("Disconnected from server\n");
    if (app->login_status_label) {
        gtk_label_set_text(GTK_LABEL(app->login_status_label), "Disconnected (Server Down?)");
    }
}

// --- Login Handlers ---

static void on_login_clicked(GtkWidget *btn, gpointer data) {
    (void)btn;
    AppWidgets *app = (AppWidgets *)data;
    const char *user = gtk_entry_get_text(GTK_ENTRY(app->login_entry_user));
    const char *pass = gtk_entry_get_text(GTK_ENTRY(app->login_entry_pass));

    if (strlen(user) == 0 || strlen(pass) == 0) {
        gtk_label_set_text(GTK_LABEL(app->login_status_label), "Username/Password required");
        return;
    }

    auth_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    strncpy(msg.username, user, sizeof(msg.username)-1);
    strncpy(msg.password, pass, sizeof(msg.password)-1);
    char *json = protocol_serialize_auth("login", &msg);
    if (json) {
        g_print("Sending login: %s\n", json);
        if (net_client_send(app->client, json, strlen(json)) < 0) {
             gtk_label_set_text(GTK_LABEL(app->login_status_label), "Send Failed: No Connection");
        } else {
             gtk_label_set_text(GTK_LABEL(app->login_status_label), "Logging in...");
        }
        free(json);
    }
}

static void on_register_submit(GtkWidget *btn, gpointer data) {
    (void)btn;
    AppWidgets *app = (AppWidgets *)data;
    const char *user = gtk_entry_get_text(GTK_ENTRY(app->login_entry_user));
    const char *pass = gtk_entry_get_text(GTK_ENTRY(app->login_entry_pass));
    const char *email = gtk_entry_get_text(GTK_ENTRY(app->login_entry_email));

    if (strlen(user) == 0 || strlen(pass) == 0 || strlen(email) == 0) {
        gtk_label_set_text(GTK_LABEL(app->login_status_label), "All fields required");
        return;
    }

    auth_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    strncpy(msg.username, user, sizeof(msg.username)-1);
    strncpy(msg.password, pass, sizeof(msg.password)-1);
    strncpy(msg.email, email, sizeof(msg.email)-1);
    
    char *json = protocol_serialize_auth("register", &msg);
    if (json) {
        g_print("Sending register: %s\n", json);
        if (net_client_send(app->client, json, strlen(json)) < 0) {
             gtk_label_set_text(GTK_LABEL(app->login_status_label), "Send Failed: No Connection");
        } else {
             gtk_label_set_text(GTK_LABEL(app->login_status_label), "Registering...");
        }
        free(json);
    }
}

static void on_switch_to_register(GtkWidget *btn, gpointer data) {
    (void)btn;
    AppWidgets *app = (AppWidgets *)data;
    gtk_widget_show(app->login_entry_email);
    gtk_widget_show(app->register_container);
    gtk_widget_hide(app->login_btn_box);
    gtk_label_set_text(GTK_LABEL(app->login_status_label), "Create Account");
}

static void on_switch_to_login(GtkWidget *btn, gpointer data) {
    (void)btn;
    AppWidgets *app = (AppWidgets *)data;
    gtk_widget_hide(app->login_entry_email);
    gtk_widget_hide(app->register_container);
    gtk_widget_show(app->login_btn_box);
    gtk_label_set_text(GTK_LABEL(app->login_status_label), "Login");
}

// --- Lobby Handlers ---

static void on_refresh_rooms_clicked(GtkWidget *btn, gpointer data) {
    (void)btn;
    AppWidgets *app = (AppWidgets *)data;
    char *json = protocol_serialize_cmd("list_rooms");
    if (json) {
        net_client_send(app->client, json, strlen(json));
        free(json);
    }
}

static void on_create_room_clicked(GtkWidget *btn, gpointer data) {
    (void)btn;
    AppWidgets *app = (AppWidgets *)data;
    const char *name = gtk_entry_get_text(GTK_ENTRY(app->create_room_entry));
    if (strlen(name) == 0) return;

    room_msg_t msg;
    msg.room_id = 0;
    strncpy(msg.name, name, sizeof(msg.name)-1);
    strncpy(msg.description, "New Room", sizeof(msg.description)-1);
    msg.owner_id = app->user_id;
    
    char *json = protocol_serialize_room("create_room", &msg);
    if (json) {
        net_client_send(app->client, json, strlen(json));
        free(json);
    }
}

static void on_join_room_clicked(GtkWidget *btn, gpointer data) {
    (void)btn;
    AppWidgets *app = (AppWidgets *)data;
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(app->room_tree_view));
    GtkTreeIter iter;
    GtkTreeModel *model;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        guint id;
        gtk_tree_model_get(model, &iter, 0, &id, -1);
        
        // Send join request
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "type", "join_room");
        cJSON *d = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "data", d);
        cJSON_AddNumberToObject(d, "room_id", id);
        char *json = cJSON_PrintUnformatted(root);
        net_client_send(app->client, json, strlen(json));
        free(json);
        cJSON_Delete(root);
    }
}

// --- Canvas Handlers ---
// (Mostly reused from previous gui_main.c)

static void on_save_local_clicked(GtkToolButton *btn, gpointer user_data) {
    (void)btn;
    AppWidgets *app = (AppWidgets *)user_data;
    
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Save Canvas",
                                      GTK_WINDOW(app->window),
                                      GTK_FILE_CHOOSER_ACTION_SAVE,
                                      "_Cancel",
                                      GTK_RESPONSE_CANCEL,
                                      "_Save",
                                      GTK_RESPONSE_ACCEPT,
                                      NULL);
    GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
    gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);
    gtk_file_chooser_set_current_name(chooser, "canvas.json");

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(chooser);
        if (filename) {
            char *data = undo_redo_serialize();
            if (data) {
                FILE *fp = fopen(filename, "w");
                if (fp) {
                    fputs(data, fp);
                    fclose(fp);
                    g_print("Saved to %s\n", filename);
                } else {
                    g_print("Failed to save to %s\n", filename);
                }
                free(data);
            }
            g_free(filename);
        }
    }
    gtk_widget_destroy(dialog);
}

static void on_load_local_clicked(GtkToolButton *btn, gpointer user_data) {
    (void)btn;
    AppWidgets *app = (AppWidgets *)user_data;
    
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Open Canvas",
                                      GTK_WINDOW(app->window),
                                      GTK_FILE_CHOOSER_ACTION_OPEN,
                                      "_Cancel",
                                      GTK_RESPONSE_CANCEL,
                                      "_Open",
                                      GTK_RESPONSE_ACCEPT,
                                      NULL);
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (filename) {
            FILE *fp = fopen(filename, "r");
            if (fp) {
                fseek(fp, 0, SEEK_END);
                long fsize = ftell(fp);
                fseek(fp, 0, SEEK_SET);
                
                char *data = malloc(fsize + 1);
                if (data) {
                    fread(data, 1, fsize, fp);
                    data[fsize] = 0;
                    
                    if (app->surface) {
                        cairo_t *cr = cairo_create(app->surface);
                        cairo_set_source_rgb(cr, 1, 1, 1);
                        cairo_paint(cr);
                        cairo_destroy(cr);
                    }
                    undo_redo_deserialize(data);
                    repaint_all(app);
                    free(data);
                    g_print("Loaded from %s\n", filename);
                }
                fclose(fp);
            }
            g_free(filename);
        }
    }
    gtk_widget_destroy(dialog);
}

static void on_save_clicked(GtkToolButton *btn, gpointer user_data) {
    (void)btn;
    AppWidgets *app = (AppWidgets *)user_data;
    if (app->client) {
        char *data = undo_redo_serialize();
        if (data) {
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "type", "save_canvas");
            cJSON_AddStringToObject(root, "data", data);
            char *json = cJSON_PrintUnformatted(root);
            net_client_send(app->client, json, strlen(json));
            free(json);
            cJSON_Delete(root);
            g_print("Saving canvas, data length: %zu\n", strlen(data));
            free(data);
        }
    }
}

static void on_tool_clicked(GtkToolButton *btn, gpointer user_data) {
    AppWidgets *app = (AppWidgets *)user_data;
    const char *label = gtk_tool_button_get_label(btn);
    if (strcmp(label, "Pen") == 0) {
        app->state.tool_type = TOOL_PEN;
        app->state.line_width = 3.0;
        app->state.color.r = 0; app->state.color.g = 0; app->state.color.b = 0;
    } else if (strcmp(label, "Eraser") == 0) {
        app->state.tool_type = TOOL_ERASER;
        app->state.line_width = 10.0;
        app->state.color.r = 1; app->state.color.g = 1; app->state.color.b = 1;
    }
}

static void on_color_set(GtkColorButton *btn, gpointer user_data) {
    AppWidgets *app = (AppWidgets *)user_data;
    GdkRGBA color;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(btn), &color);
    app->state.color.r = color.red;
    app->state.color.g = color.green;
    app->state.color.b = color.blue;
    if (app->state.tool_type == TOOL_ERASER) {
        app->state.tool_type = TOOL_PEN;
        app->state.line_width = 3.0;
    }
}

static void on_undo_clicked(GtkToolButton *btn, gpointer user_data) {
    (void)btn;
    AppWidgets *app = (AppWidgets *)user_data;
    const draw_cmd_t *cmd = undo_redo_undo();
    if (cmd) {
        repaint_all(app);
        if (app->client) {
            char *json = protocol_serialize_cmd("undo");
            if (json) { net_client_send(app->client, json, strlen(json)); free(json); }
        }
    }
}

static void on_redo_clicked(GtkToolButton *btn, gpointer user_data) {
    (void)btn;
    AppWidgets *app = (AppWidgets *)user_data;
    const draw_cmd_t *cmd = undo_redo_redo();
    if (cmd) {
        repaint_all(app);
        if (app->client) {
            char *json = protocol_serialize_cmd("redo");
            if (json) { net_client_send(app->client, json, strlen(json)); free(json); }
        }
    }
}

static void clear_surface(GtkToolButton *btn, gpointer user_data) {
    (void)btn;
    AppWidgets *app = (AppWidgets *)user_data;
    if (app->surface) {
        cairo_t *cr = cairo_create(app->surface);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_paint(cr);
        cairo_destroy(cr);
        gtk_widget_queue_draw(app->drawing_area);
        undo_redo_init(50);
        if (app->client) {
            char *json = protocol_serialize_cmd("clear");
            if (json) { net_client_send(app->client, json, strlen(json)); free(json); }
        }
    }
}

static gboolean draw_cb(GtkWidget *widget, cairo_t *cr, gpointer data) {
    (void)widget;
    AppWidgets *app = (AppWidgets *)data;
    if (app->surface) {
        cairo_set_source_surface(cr, app->surface, 0, 0);
        cairo_paint(cr);
    }
    return FALSE;
}

static void draw_brush(GtkWidget *widget, double x, double y, AppWidgets *app) {
    cairo_t *cr = cairo_create(app->surface);
    if (app->state.tool_type == TOOL_ERASER) {
        cairo_set_source_rgb(cr, 1, 1, 1);
    } else {
        cairo_set_source_rgb(cr, app->state.color.r, app->state.color.g, app->state.color.b);
    }
    cairo_set_line_width(cr, app->state.line_width);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_move_to(cr, app->state.x, app->state.y);
    cairo_line_to(cr, x, y);
    cairo_stroke(cr);
    cairo_destroy(cr);
    gtk_widget_queue_draw(widget);

    // Create command for local undo/redo history
    draw_cmd_t cmd;
    cmd.tool = app->state.tool_type;
    cmd.color = (uint32_t)((int)(app->state.color.r * 255) << 16) | ((int)(app->state.color.g * 255) << 8) | (int)(app->state.color.b * 255);
    cmd.line_width = app->state.line_width;
    cmd.point_count = 2;
    cmd.points = malloc(sizeof(point_t) * 2);
    if (cmd.points) {
        cmd.points[0].x = app->state.x; cmd.points[0].y = app->state.y;
        cmd.points[1].x = x; cmd.points[1].y = y;
        cmd.layer_id = 0;
        undo_redo_push(&cmd);
        free(cmd.points);
    }

    if (app->client) {
        draw_msg_t msg;
        msg.tool_type = app->state.tool_type;
        msg.color = cmd.color;
        msg.width = app->state.line_width;
        msg.point_count = 2;
        msg.points = malloc(sizeof(point_t) * 2);
        if (msg.points) {
            msg.points[0].x = app->state.x;
            msg.points[0].y = app->state.y;
            msg.points[1].x = x;
            msg.points[1].y = y;
            char *json = protocol_serialize_draw(&msg);
            if (json) { 
                net_client_send(app->client, json, strlen(json)); 
                free(json); 
            }
            free(msg.points);
        }
    }
    app->state.x = x;
    app->state.y = y;
}

static gboolean button_press_event_cb(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    (void)widget;
    AppWidgets *app = (AppWidgets *)data;
    if (app->surface == NULL) return FALSE;
    if (event->button == GDK_BUTTON_PRIMARY) {
        app->state.x = event->x;
        app->state.y = event->y;
        app->state.is_drawing = 1;
    }
    return TRUE;
}

static gboolean motion_notify_event_cb(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
    AppWidgets *app = (AppWidgets *)data;
    if (app->state.is_drawing) {
        draw_brush(widget, event->x, event->y, app);
    }
    return TRUE;
}

static gboolean button_release_event_cb(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    (void)widget;
    AppWidgets *app = (AppWidgets *)data;
    if (event->button == GDK_BUTTON_PRIMARY) {
        app->state.is_drawing = 0;
    }
    return TRUE;
}

static gboolean configure_event_cb(GtkWidget *widget, GdkEventConfigure *event, gpointer data) {
    (void)event;
    AppWidgets *app = (AppWidgets *)data;
    
    cairo_surface_t *old_surface = app->surface;
    
    app->surface = gdk_window_create_similar_surface(gtk_widget_get_window(widget),
                                                     CAIRO_CONTENT_COLOR,
                                                     gtk_widget_get_allocated_width(widget),
                                                     gtk_widget_get_allocated_height(widget));
                                                     
    cairo_t *cr = cairo_create(app->surface);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    
    // Copy old surface if exists, or repaint from history
    // Since we have full history, it's better to repaint from history to handle resizing properly (vectors)
    // But if history is large, it might be slow. 
    // For now, let's just repaint from history which is the "correct" way for a vector drawing app.
    
    cairo_destroy(cr);
    
    if (old_surface) cairo_surface_destroy(old_surface);
    
    // Repaint everything on the new surface
    repaint_all(app);
    
    return TRUE;
}

static void on_chat_send(GtkWidget *widget, gpointer data) {
    (void)widget;
    AppWidgets *app = (AppWidgets *)data;
    const char *text = gtk_entry_get_text(GTK_ENTRY(app->chat_entry));
    if (strlen(text) == 0) return;
    
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(app->chat_buffer, &end);
    char msg[512];
    snprintf(msg, sizeof(msg), "Me: %s\n", text);
    gtk_text_buffer_insert(app->chat_buffer, &end, msg, -1);
    
    if (app->client) {
        chat_msg_t chat_msg;
        strncpy(chat_msg.sender, app->username, sizeof(chat_msg.sender)-1);
        strncpy(chat_msg.content, text, sizeof(chat_msg.content)-1);
        chat_msg.timestamp = 0;
        char *json = protocol_serialize_chat(&chat_msg);
        if (json) { net_client_send(app->client, json, strlen(json)); free(json); }
    }
    gtk_entry_set_text(GTK_ENTRY(app->chat_entry), "");
}

static void repaint_cmd_cb(const draw_cmd_t *cmd, void *user_data) {
    AppWidgets *app = (AppWidgets *)user_data;
    if (!app->surface) return;
    cairo_t *cr = cairo_create(app->surface);
    if (cmd->tool == TOOL_ERASER) cairo_set_source_rgb(cr, 1, 1, 1);
    else cairo_set_source_rgb(cr, ((cmd->color >> 16) & 0xFF)/255.0, ((cmd->color >> 8) & 0xFF)/255.0, (cmd->color & 0xFF)/255.0);
    cairo_set_line_width(cr, cmd->line_width);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    if (cmd->point_count > 1 && cmd->points) {
        cairo_move_to(cr, cmd->points[0].x, cmd->points[0].y);
        for (size_t i = 1; i < cmd->point_count; i++) cairo_line_to(cr, cmd->points[i].x, cmd->points[i].y);
        cairo_stroke(cr);
    }
    cairo_destroy(cr);
}

static void repaint_all(AppWidgets *app) {
    if (!app->surface) return;
    cairo_t *cr = cairo_create(app->surface);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_destroy(cr);
    undo_redo_iterate(repaint_cmd_cb, app);
    gtk_widget_queue_draw(app->drawing_area);
}

// --- Message Handler ---

static gboolean update_ui_idle(gpointer user_data) {
    AppWidgets *app = (AppWidgets *)user_data;
    if (app->phase == APP_PHASE_LOBBY) {
        build_lobby_ui(app);
        // Force window resize to trigger layout recalculation
        gtk_window_resize(GTK_WINDOW(app->window), 800, 600);
        gtk_widget_queue_draw(app->window);
    } else if (app->phase == APP_PHASE_CANVAS) {
        build_canvas_ui(app);
        gtk_widget_queue_draw(app->window);
    } else if (app->phase == APP_PHASE_LOGIN) {
        // Handle login UI updates if needed
        // For example, if we got a login error, we might want to show a dialog or update a label
        // But for now, direct GTK calls in on_net_message seem to work for labels,
        // unless they are from a different thread.
        // GLib main loop callbacks run in the main thread, so it should be fine.
    }
    return FALSE; // Remove source
}

static void on_net_message(const char *msg, size_t len, void *user_data) {
    (void)len;
    AppWidgets *app = (AppWidgets *)user_data;
    
     g_print("RX: %.*s\n", (int)len, msg);  // 使用长度限制打印

     if (!msg || len == 0) {
        g_print("Warning: 空消息被接收\n");
        return;
    }

    // Check if it's a JSON message
    cJSON *root = cJSON_Parse(msg);
    if (!root) {
        g_print("JSON Parse Failed for message: %.*s\n", (int)len, msg);
        return;
    }

    cJSON *type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type)) { cJSON_Delete(root); return; }

    // Debug print
    g_print("Msg Type: %s\n", type->valuestring);

    if (app->phase == APP_PHASE_LOGIN) {
        if (strcmp(type->valuestring, "login_resp") == 0) {
            cJSON *status = cJSON_GetObjectItem(root, "status");
            g_print("Login Status: %s\n", status ? status->valuestring : "null");
            
            if (status && strcmp(status->valuestring, "ok") == 0) {
                app->username[0] = '\0';
                strncpy(app->username, gtk_entry_get_text(GTK_ENTRY(app->login_entry_user)), sizeof(app->username)-1);
                
                g_print("Login OK, switching to Lobby...\n");
                
                // Ensure phase is updated BEFORE building UI
                app->phase = APP_PHASE_LOBBY;
                
                // Use g_idle_add to update UI safely
                g_idle_add(update_ui_idle, app);
            } else {
                cJSON *msg_item = cJSON_GetObjectItem(root, "message");
                const char *err_msg = msg_item && msg_item->valuestring ? msg_item->valuestring : "Login Failed";
                g_print("Login Error: %s\n", err_msg);
                
                gtk_label_set_text(GTK_LABEL(app->login_status_label), err_msg);
            }
        } else if (strcmp(type->valuestring, "register_resp") == 0) {
            cJSON *status = cJSON_GetObjectItem(root, "status");
            g_print("Register Status: %s\n", status ? status->valuestring : "null");

            if (status && strcmp(status->valuestring, "ok") == 0) {
                // Switch back to login view automatically for better UX
                on_switch_to_login(NULL, app);
                gtk_label_set_text(GTK_LABEL(app->login_status_label), "Registered! Please Login.");
            } else {
                cJSON *msg_item = cJSON_GetObjectItem(root, "message");
                const char *err_msg = msg_item && msg_item->valuestring ? msg_item->valuestring : "Registration Failed";
                g_print("Register Error: %s\n", err_msg);
                gtk_label_set_text(GTK_LABEL(app->login_status_label), err_msg);
            }
        }
    } else if (app->phase == APP_PHASE_LOBBY) {
        if (strcmp(type->valuestring, "room_list") == 0) {
            gtk_list_store_clear(app->room_list_store);
            cJSON *data = cJSON_GetObjectItem(root, "data");
            cJSON *item = NULL;
            cJSON_ArrayForEach(item, data) {
                cJSON *id = cJSON_GetObjectItem(item, "id");
                cJSON *name = cJSON_GetObjectItem(item, "name");
                if (id && name) {
                    GtkTreeIter iter;
                    gtk_list_store_append(app->room_list_store, &iter);
                    gtk_list_store_set(app->room_list_store, &iter, 0, (guint)id->valuedouble, 1, name->valuestring, -1);
                }
            }
        } else if (strcmp(type->valuestring, "create_room_resp") == 0) {
            on_refresh_rooms_clicked(NULL, app); // Refresh list
        } else if (strcmp(type->valuestring, "join_room_resp") == 0) {
            cJSON *status = cJSON_GetObjectItem(root, "status");
            if (status && strcmp(status->valuestring, "ok") == 0) {
                app->phase = APP_PHASE_CANVAS;
                g_idle_add(update_ui_idle, app);
            }
        }
    } else if (app->phase == APP_PHASE_CANVAS) {
        // Draw, Chat, Undo, Redo, Clear
        draw_msg_t draw_msg;
        if (protocol_deserialize_draw(msg, &draw_msg) == 0) {
            cairo_t *cr = cairo_create(app->surface);
            if (draw_msg.tool_type == TOOL_ERASER) cairo_set_source_rgb(cr, 1, 1, 1);
            else cairo_set_source_rgb(cr, ((draw_msg.color >> 16) & 0xFF)/255.0, ((draw_msg.color >> 8) & 0xFF)/255.0, (draw_msg.color & 0xFF)/255.0);
            cairo_set_line_width(cr, draw_msg.width);
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
            cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
            if (draw_msg.point_count > 1 && draw_msg.points) {
                cairo_move_to(cr, draw_msg.points[0].x, draw_msg.points[0].y);
                for (size_t i = 1; i < draw_msg.point_count; i++) cairo_line_to(cr, draw_msg.points[i].x, draw_msg.points[i].y);
                cairo_stroke(cr);
            }
            cairo_destroy(cr);
            gtk_widget_queue_draw(app->drawing_area);
            
            draw_cmd_t cmd;
            cmd.tool = (tool_type_t)draw_msg.tool_type;
            cmd.color = draw_msg.color;
            cmd.line_width = draw_msg.width;
            cmd.point_count = draw_msg.point_count;
            cmd.points = (point_t *)draw_msg.points; 
            cmd.layer_id = 0;
            undo_redo_push(&cmd);
            protocol_free_draw_msg(&draw_msg);
        } else {
            chat_msg_t chat_msg;
            if (protocol_deserialize_chat(msg, &chat_msg) == 0) {
                if (strcmp(chat_msg.sender, app->username) != 0) {
                    GtkTextIter end;
                    gtk_text_buffer_get_end_iter(app->chat_buffer, &end);
                    char display_msg[512];
                    snprintf(display_msg, sizeof(display_msg), "%.32s: %.440s\n", chat_msg.sender, chat_msg.content);
                    gtk_text_buffer_insert(app->chat_buffer, &end, display_msg, -1);
                }
            } else if (strcmp(type->valuestring, "undo") == 0) {
                const draw_cmd_t *cmd = undo_redo_undo();
                if (cmd) repaint_all(app);
            } else if (strcmp(type->valuestring, "redo") == 0) {
                const draw_cmd_t *cmd = undo_redo_redo();
                if (cmd) repaint_all(app);
            } else if (strcmp(type->valuestring, "clear") == 0) {
                if (app->surface) {
                    cairo_t *cr = cairo_create(app->surface);
                    cairo_set_source_rgb(cr, 1, 1, 1);
                    cairo_paint(cr);
                    cairo_destroy(cr);
                    gtk_widget_queue_draw(app->drawing_area);
                    undo_redo_init(50);
                }
            } else if (strcmp(type->valuestring, "load_canvas") == 0) {
                cJSON *data = cJSON_GetObjectItem(root, "data");
                if (data && cJSON_IsString(data)) {
                    if (app->surface) {
                        cairo_t *cr = cairo_create(app->surface);
                        cairo_set_source_rgb(cr, 1, 1, 1);
                        cairo_paint(cr);
                        cairo_destroy(cr);
                    }
                    undo_redo_deserialize(data->valuestring);
                    repaint_all(app);
                }
            }
        }
    }
    cJSON_Delete(root);
}

// --- UI Builders ---

static void clear_container(AppWidgets *app) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(app->main_container));
    for (GList *iter = children; iter != NULL; iter = g_list_next(iter)) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);
    
    // Reset widget pointers to avoid using destroyed widgets
    app->login_entry_user = NULL;
    app->login_entry_pass = NULL;
    app->login_entry_email = NULL;
    app->login_btn_box = NULL;
    app->register_container = NULL;
    app->login_status_label = NULL;
    app->create_room_entry = NULL;
    app->room_tree_view = NULL;
    app->room_list_store = NULL;
}

static void build_login_ui(AppWidgets *app) {
    app->phase = APP_PHASE_LOGIN;
    clear_container(app);
    
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    gtk_container_add(GTK_CONTAINER(app->main_container), box);

    GtkWidget *label = gtk_label_new("Login to Canvas");
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 10);

    app->login_entry_user = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->login_entry_user), "Username");
    gtk_box_pack_start(GTK_BOX(box), app->login_entry_user, FALSE, FALSE, 0);

    app->login_entry_pass = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->login_entry_pass), "Password");
    gtk_entry_set_visibility(GTK_ENTRY(app->login_entry_pass), FALSE);
    g_signal_connect(app->login_entry_pass, "activate", G_CALLBACK(on_login_clicked), app);
    gtk_box_pack_start(GTK_BOX(box), app->login_entry_pass, FALSE, FALSE, 0);

    // Email (hidden by default)
    app->login_entry_email = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->login_entry_email), "Email");
    gtk_box_pack_start(GTK_BOX(box), app->login_entry_email, FALSE, FALSE, 0);

    // Login Buttons Container
    app->login_btn_box = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(box), app->login_btn_box, FALSE, FALSE, 10);

    GtkWidget *login_btn = gtk_button_new_with_label("Login");
    // CRITICAL: Ensure we use 'clicked' for button
    g_signal_connect(login_btn, "clicked", G_CALLBACK(on_login_clicked), app);
    gtk_container_add(GTK_CONTAINER(app->login_btn_box), login_btn);

    GtkWidget *goto_reg_btn = gtk_button_new_with_label("Go to Register");
    g_signal_connect(goto_reg_btn, "clicked", G_CALLBACK(on_switch_to_register), app);
    gtk_container_add(GTK_CONTAINER(app->login_btn_box), goto_reg_btn);

    // Register Buttons Container (hidden by default)
    app->register_container = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(box), app->register_container, FALSE, FALSE, 10);
    
    GtkWidget *reg_submit_btn = gtk_button_new_with_label("Register");
    g_signal_connect(reg_submit_btn, "clicked", G_CALLBACK(on_register_submit), app);
    gtk_container_add(GTK_CONTAINER(app->register_container), reg_submit_btn);

    GtkWidget *back_login_btn = gtk_button_new_with_label("Back to Login");
    g_signal_connect(back_login_btn, "clicked", G_CALLBACK(on_switch_to_login), app);
    gtk_container_add(GTK_CONTAINER(app->register_container), back_login_btn);
    
    // Initial State: Login Mode
    // CRITICAL: Ensure widgets are realized before hiding/showing if using show_all later
    // But since we use show_all at the end, we need to hide AFTER show_all
    
    app->login_status_label = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(box), app->login_status_label, FALSE, FALSE, 0);

    gtk_widget_show_all(app->window);
    
    // HIDE widgets that should be hidden initially
    // We must do this AFTER show_all because show_all recursively shows everything
    gtk_widget_hide(app->login_entry_email);
    gtk_widget_hide(app->register_container);
    
    // Explicitly SHOW widgets that should be visible
    gtk_widget_show(app->login_btn_box);
    
    // Auto-fill username if available
    if (strlen(app->username) > 0) {
        gtk_entry_set_text(GTK_ENTRY(app->login_entry_user), app->username);
    }
}

static void build_lobby_ui(AppWidgets *app) {
    app->phase = APP_PHASE_LOBBY;
    clear_container(app);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(app->main_container), vbox);

    // Header
    char title[128];
    snprintf(title, sizeof(title), "Welcome, %s", app->username);
    GtkWidget *header = gtk_label_new(title);
    gtk_box_pack_start(GTK_BOX(vbox), header, FALSE, FALSE, 10);

    // Room List
    app->room_list_store = gtk_list_store_new(2, G_TYPE_UINT, G_TYPE_STRING);
    app->room_tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(app->room_list_store));
    
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes("Room Name", renderer, "text", 1, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(app->room_tree_view), col);

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(scrolled, -1, 300); // Set minimum height
    gtk_container_add(GTK_CONTAINER(scrolled), app->room_tree_view);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);

    // Controls
    GtkWidget *ctrl_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), ctrl_box, FALSE, FALSE, 5);

    GtkWidget *refresh_btn = gtk_button_new_with_label("Refresh");
    g_signal_connect(refresh_btn, "clicked", G_CALLBACK(on_refresh_rooms_clicked), app);
    gtk_box_pack_start(GTK_BOX(ctrl_box), refresh_btn, FALSE, FALSE, 0);

    GtkWidget *join_btn = gtk_button_new_with_label("Join");
    g_signal_connect(join_btn, "clicked", G_CALLBACK(on_join_room_clicked), app);
    gtk_box_pack_start(GTK_BOX(ctrl_box), join_btn, FALSE, FALSE, 0);

    app->create_room_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->create_room_entry), "New Room Name");
    gtk_box_pack_start(GTK_BOX(ctrl_box), app->create_room_entry, TRUE, TRUE, 0);

    GtkWidget *create_btn = gtk_button_new_with_label("Create");
    g_signal_connect(create_btn, "clicked", G_CALLBACK(on_create_room_clicked), app);
    gtk_box_pack_start(GTK_BOX(ctrl_box), create_btn, FALSE, FALSE, 0);

    gtk_widget_show_all(app->window);
    
    // Auto refresh
    on_refresh_rooms_clicked(NULL, app);
}

static void build_canvas_ui(AppWidgets *app) {
    app->phase = APP_PHASE_CANVAS;
    clear_container(app);
    undo_redo_init(50);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(app->main_container), vbox);

    // Toolbar
    GtkWidget *toolbar = gtk_toolbar_new();
    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);

    GtkToolItem *pen = gtk_tool_button_new(NULL, "Pen");
    g_signal_connect(pen, "clicked", G_CALLBACK(on_tool_clicked), app);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), pen, -1);

    GtkToolItem *eraser = gtk_tool_button_new(NULL, "Eraser");
    g_signal_connect(eraser, "clicked", G_CALLBACK(on_tool_clicked), app);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), eraser, -1);

    GtkToolItem *sep = gtk_separator_tool_item_new();
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), sep, -1);

    GtkToolItem *color_item = gtk_tool_item_new();
    GtkWidget *color_btn = gtk_color_button_new();
    g_signal_connect(color_btn, "color-set", G_CALLBACK(on_color_set), app);
    gtk_container_add(GTK_CONTAINER(color_item), color_btn);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), color_item, -1);

    GtkToolItem *undo = gtk_tool_button_new(NULL, "Undo");
    g_signal_connect(undo, "clicked", G_CALLBACK(on_undo_clicked), app);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), undo, -1);

    GtkToolItem *redo = gtk_tool_button_new(NULL, "Redo");
    g_signal_connect(redo, "clicked", G_CALLBACK(on_redo_clicked), app);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), redo, -1);

    GtkToolItem *clear = gtk_tool_button_new(NULL, "Clear");
    g_signal_connect(clear, "clicked", G_CALLBACK(clear_surface), app);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), clear, -1);

    GtkToolItem *save = gtk_tool_button_new(NULL, "Save");
    g_signal_connect(save, "clicked", G_CALLBACK(on_save_clicked), app);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), save, -1);

    GtkToolItem *save_local = gtk_tool_button_new(NULL, "Save Local");
    g_signal_connect(save_local, "clicked", G_CALLBACK(on_save_local_clicked), app);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), save_local, -1);

    GtkToolItem *load_local = gtk_tool_button_new(NULL, "Load Local");
    g_signal_connect(load_local, "clicked", G_CALLBACK(on_load_local_clicked), app);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), load_local, -1);

    // Split Pane
    GtkWidget *hpaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), hpaned, TRUE, TRUE, 0);

    // Drawing Area
    app->drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(app->drawing_area, 600, 400);
    // Use gtk_box_pack_start instead of paned for simplicity if paned is causing issues,
    // but paned should be fine if we set hexpand/vexpand
    gtk_widget_set_hexpand(app->drawing_area, TRUE);
    gtk_widget_set_vexpand(app->drawing_area, TRUE);
    gtk_paned_pack1(GTK_PANED(hpaned), app->drawing_area, TRUE, TRUE);

    g_signal_connect(app->drawing_area, "draw", G_CALLBACK(draw_cb), app);
    g_signal_connect(app->drawing_area, "configure-event", G_CALLBACK(configure_event_cb), app);
    g_signal_connect(app->drawing_area, "button-press-event", G_CALLBACK(button_press_event_cb), app);
    g_signal_connect(app->drawing_area, "motion-notify-event", G_CALLBACK(motion_notify_event_cb), app);
    g_signal_connect(app->drawing_area, "button-release-event", G_CALLBACK(button_release_event_cb), app);
    gtk_widget_add_events(app->drawing_area, GDK_BUTTON_PRESS_MASK | GDK_POINTER_MOTION_MASK | GDK_BUTTON_RELEASE_MASK);

    // Chat Area
    GtkWidget *chat_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_size_request(chat_vbox, 200, -1);
    
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(chat_vbox), scrolled, TRUE, TRUE, 0);
    
    GtkWidget *chat_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(chat_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(chat_view), FALSE);
    app->chat_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(chat_view));
    gtk_container_add(GTK_CONTAINER(scrolled), chat_view);

    GtkWidget *entry_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(chat_vbox), entry_box, FALSE, FALSE, 0);

    app->chat_entry = gtk_entry_new();
    g_signal_connect(app->chat_entry, "activate", G_CALLBACK(on_chat_send), app);
    gtk_box_pack_start(GTK_BOX(entry_box), app->chat_entry, TRUE, TRUE, 0);

    GtkWidget *send_btn = gtk_button_new_with_label("Send");
    g_signal_connect(send_btn, "clicked", G_CALLBACK(on_chat_send), app);
    gtk_box_pack_start(GTK_BOX(entry_box), send_btn, FALSE, FALSE, 0);

    gtk_paned_pack2(GTK_PANED(hpaned), chat_vbox, FALSE, FALSE);
    gtk_paned_set_position(GTK_PANED(hpaned), 600);

    gtk_widget_show_all(app->window);
}

static void activate(GtkApplication *app, gpointer user_data) {
    AppWidgets *widgets = (AppWidgets *)user_data;

    widgets->window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(widgets->window), "Canvas Client (Linux)");
    gtk_window_set_default_size(GTK_WINDOW(widgets->window), 800, 600);

    widgets->main_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(widgets->window), widgets->main_container);

    build_login_ui(widgets);
}

int main(int argc, char **argv) {
    GtkApplication *app;
    int status;
    static AppWidgets widgets = {0};
    
    // Defaults
    widgets.state.line_width = 3.0;
    widgets.state.tool_type = TOOL_PEN;

    char app_id[64];
    snprintf(app_id, sizeof(app_id), "org.canvas.client.pid%d", getpid());
    app = gtk_application_new(app_id, G_APPLICATION_FLAGS_NONE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), &widgets);
    
    widgets.client = net_client_new("ws://localhost:8080");
    if (widgets.client) {
        net_client_set_callbacks(widgets.client, (net_callback_t)on_net_connect, (net_callback_t)on_net_disconnect, (net_callback_t)on_net_message, &widgets);
        net_client_connect(widgets.client);
    }
    
    status = g_application_run(G_APPLICATION(app), argc, argv);
    
    if (widgets.client) {
        net_client_free(widgets.client);
    }
    g_object_unref(app);

    return status;
}
