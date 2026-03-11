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
#include "undo_redo.h"
#include "common/core_logic.h"

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
    char server_ip[64];
    int server_port;
    
    guint auto_save_timer;
    int needs_save;
} AppWidgets;

// Forward declarations
static void build_login_ui(AppWidgets *app);
static void build_lobby_ui(AppWidgets *app);
static void build_canvas_ui(AppWidgets *app);
static void repaint_all(AppWidgets *app);
static void on_chat_history(chat_msg_t *messages, int count, void *user_data);
static gboolean auto_save_canvas(gpointer user_data);

// --- Auto Save ---

static gboolean auto_save_canvas(gpointer user_data) {
    AppWidgets *app = (AppWidgets *)user_data;
    
    g_print("DEBUG: auto_save_canvas - needs_save=%d, current_room_id=%u\n", 
           app->needs_save, app->current_room_id);
    
    if (app->needs_save && app->current_room_id > 0) {
        g_print("INFO: Auto-saving canvas...\n");
        core_save_canvas();
        app->needs_save = 0;
    }
    
    return G_SOURCE_CONTINUE;
}

static void mark_needs_save(AppWidgets *app) {
    g_print("DEBUG: mark_needs_save called, setting needs_save=1\n");
    app->needs_save = 1;
}

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

    gtk_label_set_text(GTK_LABEL(app->login_status_label), "Logging in...");
    core_login(user, pass);
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

    gtk_label_set_text(GTK_LABEL(app->login_status_label), "Registering...");
    core_register(user, pass, email);
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
    (void)data;
    core_list_rooms();
}

static void on_create_room_clicked(GtkWidget *btn, gpointer data) {
    (void)btn;
    AppWidgets *app = (AppWidgets *)data;
    const char *name = gtk_entry_get_text(GTK_ENTRY(app->create_room_entry));
    if (strlen(name) == 0) return;

    core_create_room(name, "New Room");
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
        app->current_room_id = id;
        core_join_room(id);
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
    (void)user_data;
    g_print("INFO: Starting canvas save operation...\n");
    core_save_canvas();
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
        core_send_undo();
        mark_needs_save(app);
    }
}

static void on_redo_clicked(GtkToolButton *btn, gpointer user_data) {
    (void)btn;
    AppWidgets *app = (AppWidgets *)user_data;
    const draw_cmd_t *cmd = undo_redo_redo();
    if (cmd) {
        repaint_all(app);
        core_send_redo();
        mark_needs_save(app);
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
        core_send_clear();
        mark_needs_save(app);
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

    // 添加点到当前操作
    undo_redo_add_point(x, y);

    if (app->client) {
        draw_msg_t msg;
        msg.tool_type = app->state.tool_type;
        if (app->state.tool_type == TOOL_ERASER) {
            msg.color = 0xFFFFFF;
        } else {
            msg.color = (uint32_t)((int)(app->state.color.r * 255) << 16) | ((int)(app->state.color.g * 255) << 8) | (int)(app->state.color.b * 255);
        }
        msg.width = app->state.line_width;
        msg.point_count = 2;
        msg.points = malloc(sizeof(point_t) * 2);
        if (msg.points) {
            msg.points[0].x = app->state.x;
            msg.points[0].y = app->state.y;
            msg.points[1].x = x;
            msg.points[1].y = y;
            core_send_draw(&msg);
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
        uint32_t color;
        if (app->state.tool_type == TOOL_ERASER) {
            color = 0xFFFFFF;
        } else {
            color = (uint32_t)((int)(app->state.color.r * 255) << 16) | ((int)(app->state.color.g * 255) << 8) | (int)(app->state.color.b * 255);
        }
        undo_redo_start_action(app->state.tool_type, color, app->state.line_width);
        undo_redo_add_point(app->state.x, app->state.y);
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
        undo_redo_end_action();
        mark_needs_save(app);
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
    
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[16];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);
    
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(app->chat_buffer, &end);
    char msg[768];
    snprintf(msg, sizeof(msg), "Me [%s]:\n  %s\n", time_str, text);
    gtk_text_buffer_insert(app->chat_buffer, &end, msg, -1);
    
    if (app->client) {
        core_send_chat(text);
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

// 延迟重绘函数
static gboolean delayed_repaint(gpointer user_data) {
    AppWidgets *app = (AppWidgets *)user_data;
    
    if (app->surface) {
        g_print("INFO: Delayed repaint, surface is now ready\n");
        repaint_all(app);
    } else {
        g_print("WARNING: Surface still not ready after delay\n");
    }
    
    return FALSE; // 只执行一次
}

static gboolean batch_repaint(gpointer user_data) {
    AppWidgets *app = (AppWidgets *)user_data;
    static int current_cmd = 0;
    static guint idle_id = 0;
    const int batch_size = 10;
    
    if (!app->surface) {
        current_cmd = 0;
        idle_id = 0;
        return FALSE;
    }
    
    int total_cmds = canvas_get_command_count();
    
    if (current_cmd == 0 && total_cmds == 0) {
        cairo_t *cr = cairo_create(app->surface);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_paint(cr);
        cairo_destroy(cr);
        gtk_widget_queue_draw(app->drawing_area);
        idle_id = 0;
        g_print("INFO: Batch repaint completed (empty canvas)\n");
        return FALSE;
    }
    
    int end_cmd = current_cmd + batch_size;
    if (end_cmd > total_cmds) {
        end_cmd = total_cmds;
    }
    
    if (current_cmd == 0) {
        cairo_t *cr = cairo_create(app->surface);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_paint(cr);
        cairo_destroy(cr);
    }
    
    g_print("INFO: Batch repaint: processing commands %d-%d of %d\n", current_cmd, end_cmd-1, total_cmds);
    
    undo_redo_iterate_range(current_cmd, batch_size, repaint_cmd_cb, app);
    gtk_widget_queue_draw(app->drawing_area);
    
    current_cmd = end_cmd;
    
    if (current_cmd >= total_cmds) {
        current_cmd = 0;
        idle_id = 0;
        g_print("INFO: Batch repaint completed\n");
        return FALSE;
    }
    
    return TRUE;
}

static void repaint_all(AppWidgets *app) {
    if (!app->surface) {
        g_print("WARNING: Cannot repaint, surface is NULL\n");
        return;
    }
    
    g_print("INFO: Starting batch repaint\n");
    g_idle_add(batch_repaint, app);
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
    }
    return FALSE; // Remove source
}

// 核心逻辑回调函数
static void on_login_success(void *user_data) {
    AppWidgets *app = (AppWidgets *)user_data;
    app->username[0] = '\0';
    strncpy(app->username, gtk_entry_get_text(GTK_ENTRY(app->login_entry_user)), sizeof(app->username)-1);
    
    g_print("Login OK, switching to Lobby...\n");
    
    // Ensure phase is updated BEFORE building UI
    app->phase = APP_PHASE_LOBBY;
    
    // Use g_idle_add to update UI safely
    g_idle_add(update_ui_idle, app);
}

static void on_login_failure(const char *error, void *user_data) {
    AppWidgets *app = (AppWidgets *)user_data;
    g_print("Login Error: %s\n", error);
    gtk_label_set_text(GTK_LABEL(app->login_status_label), error);
}

static void on_register_success(void *user_data) {
    AppWidgets *app = (AppWidgets *)user_data;
    // Switch back to login view automatically for better UX
    on_switch_to_login(NULL, app);
    gtk_label_set_text(GTK_LABEL(app->login_status_label), "Registered! Please Login.");
}

static void on_register_failure(const char *error, void *user_data) {
    AppWidgets *app = (AppWidgets *)user_data;
    g_print("Register Error: %s\n", error);
    gtk_label_set_text(GTK_LABEL(app->login_status_label), error);
}

static void on_room_list(room_list_msg_t *list, void *user_data) {
    AppWidgets *app = (AppWidgets *)user_data;
    gtk_list_store_clear(app->room_list_store);
    
    for (int i = 0; i < list->count; i++) {
        room_msg_t *room = &list->rooms[i];
        GtkTreeIter iter;
        gtk_list_store_append(app->room_list_store, &iter);
        gtk_list_store_set(app->room_list_store, &iter, 0, room->room_id, 1, room->name, -1);
    }
}

static void on_join_room_success(void *user_data) {
    AppWidgets *app = (AppWidgets *)user_data;
    undo_redo_init(50);
    app->needs_save = 0;
    g_print("DEBUG: join_room_resp OK, phase set to CANVAS\n");
    app->phase = APP_PHASE_CANVAS;
    g_idle_add(update_ui_idle, app);

    g_print("INFO: Waiting for canvas data...\n");
    
    if (app->auto_save_timer == 0) {
        app->auto_save_timer = g_timeout_add(5000, auto_save_canvas, app);
        g_print("INFO: Auto-save timer started (5 second interval)\n");
    }
}

static void on_save_canvas_success(void *user_data) {
    AppWidgets *app = (AppWidgets *)user_data;
    g_print("INFO: Canvas saved successfully!\n");
}

static void on_save_canvas_failure(const char *error, void *user_data) {
    AppWidgets *app = (AppWidgets *)user_data;
    g_print("ERROR: Canvas save failed: %s\n", error);
}

static gboolean on_window_close(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    AppWidgets *app = (AppWidgets *)user_data;
    
    g_print("DEBUG: on_window_close - needs_save=%d, current_room_id=%u\n", 
           app->needs_save, app->current_room_id);
    
    if (app->needs_save && app->current_room_id > 0) {
        g_print("INFO: Saving canvas before exit...\n");
        core_save_canvas();
        app->needs_save = 0;
        
        if (app->client) {
            g_print("INFO: Waiting for save to complete...\n");
            g_usleep(500000);
        }
    }
    
    if (app->auto_save_timer > 0) {
        g_source_remove(app->auto_save_timer);
        app->auto_save_timer = 0;
    }
    
    return FALSE;
}

static void on_draw(draw_msg_t *msg, void *user_data) {
    AppWidgets *app = (AppWidgets *)user_data;
    if (!app->surface) {
        g_print("WARNING: on_draw called but surface is NULL\n");
        return;
    }
    cairo_t *cr = cairo_create(app->surface);
    if (msg->tool_type == TOOL_ERASER) cairo_set_source_rgb(cr, 1, 1, 1);
    else cairo_set_source_rgb(cr, ((msg->color >> 16) & 0xFF)/255.0, ((msg->color >> 8) & 0xFF)/255.0, (msg->color & 0xFF)/255.0);
    cairo_set_line_width(cr, msg->width);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    if (msg->point_count > 1 && msg->points) {
        cairo_move_to(cr, msg->points[0].x, msg->points[0].y);
        for (size_t i = 1; i < msg->point_count; i++) cairo_line_to(cr, msg->points[i].x, msg->points[i].y);
        cairo_stroke(cr);
    }
    cairo_destroy(cr);
    gtk_widget_queue_draw(app->drawing_area);
}

static void on_chat(chat_msg_t *msg, void *user_data) {
    AppWidgets *app = (AppWidgets *)user_data;
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(app->chat_buffer, &end);
    
    time_t now = msg->timestamp > 0 ? (time_t)msg->timestamp : time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[16];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);
    
    char display_msg[768];
    if (strcmp(msg->sender, app->username) == 0) {
        snprintf(display_msg, sizeof(display_msg), "Me [%s]:\n  %s\n", time_str, msg->content);
    } else {
        snprintf(display_msg, sizeof(display_msg), "%.32s [%s]:\n  %s\n", msg->sender, time_str, msg->content);
    }
    gtk_text_buffer_insert(app->chat_buffer, &end, display_msg, -1);
}

static void on_undo(void *user_data) {
    AppWidgets *app = (AppWidgets *)user_data;
    if (!app->surface) return;
    const draw_cmd_t *cmd = undo_redo_undo();
    if (cmd) repaint_all(app);
}

static void on_redo(void *user_data) {
    AppWidgets *app = (AppWidgets *)user_data;
    if (!app->surface) return;
    const draw_cmd_t *cmd = undo_redo_redo();
    if (cmd) repaint_all(app);
}

static void on_clear(void *user_data) {
    AppWidgets *app = (AppWidgets *)user_data;
    if (app->surface) {
        cairo_t *cr = cairo_create(app->surface);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_paint(cr);
        cairo_destroy(cr);
        gtk_widget_queue_draw(app->drawing_area);
        undo_redo_init(50);
    }
}

static void on_load_canvas(const char *data, void *user_data) {
    AppWidgets *app = (AppWidgets *)user_data;
    g_print("INFO: Received load_canvas, data length: %zu\n", strlen(data));
    
    if (strlen(data) == 0 || strcmp(data, "[]") == 0) {
        g_print("INFO: No canvas data to load\n");
        if (app->surface) {
            cairo_t *cr = cairo_create(app->surface);
            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_paint(cr);
            cairo_destroy(cr);
        }
        undo_redo_init(50);
        if (app->drawing_area && GTK_IS_WIDGET(app->drawing_area)) {
            gtk_widget_queue_draw(app->drawing_area);
        }
    } else {
        g_print("INFO: Deserializing canvas data...\n");
        undo_redo_deserialize(data);
        int cmd_count = canvas_get_command_count();
        g_print("INFO: Canvas data deserialized, command count: %d\n", cmd_count);
        
        if (app->surface) {
            g_print("INFO: Repainting canvas with %d commands\n", cmd_count);
            repaint_all(app);
        } else {
            g_print("WARNING: Surface not ready, scheduling delayed repaint\n");
            g_timeout_add(200, (GSourceFunc)delayed_repaint, app);
        }
    }
}

static void on_user_joined(const char *username, uint32_t user_id, void *user_data) {
    AppWidgets *app = (AppWidgets *)user_data;
    (void)user_id;
    
    if (app->chat_buffer) {
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(app->chat_buffer, &end);
        
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char time_str[16];
        strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);
        
        char notification[256];
        snprintf(notification, sizeof(notification), "[System] [%s]:\n  %s joined the room.\n", time_str, username);
        gtk_text_buffer_insert(app->chat_buffer, &end, notification, -1);
    }
}

static void on_chat_history(chat_msg_t *messages, int count, void *user_data) {
    AppWidgets *app = (AppWidgets *)user_data;
    
    if (!app->chat_buffer || !messages || count <= 0) return;
    
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(app->chat_buffer, &end);
    
    gtk_text_buffer_insert(app->chat_buffer, &end, "--- Chat History ---\n", -1);
    
    for (int i = 0; i < count; i++) {
        time_t msg_time = messages[i].timestamp > 0 ? (time_t)messages[i].timestamp : time(NULL);
        struct tm *tm_info = localtime(&msg_time);
        char time_str[16];
        strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);
        
        char display_msg[768];
        if (strcmp(messages[i].sender, app->username) == 0) {
            snprintf(display_msg, sizeof(display_msg), "Me [%s]:\n  %s\n", time_str, messages[i].content);
        } else {
            snprintf(display_msg, sizeof(display_msg), "%.32s [%s]:\n  %s\n", messages[i].sender, time_str, messages[i].content);
        }
        gtk_text_buffer_insert(app->chat_buffer, &end, display_msg, -1);
        gtk_text_buffer_get_end_iter(app->chat_buffer, &end);
    }
    
    gtk_text_buffer_insert(app->chat_buffer, &end, "-------------------\n", -1);
}

static void on_net_message(const char *msg, size_t len, void *user_data) {
    (void)user_data;
    g_print("RX: %.*s\n", (int)len, msg);
    core_handle_message(msg, len);
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
    app->login_status_label = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(box), app->login_status_label, FALSE, FALSE, 0);

    gtk_widget_show_all(app->window);
    
    // HIDE widgets that should be hidden initially
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
    // undo_redo_init(50); // Removed to prevent wiping history loaded from network

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

    if (canvas_has_history()) {
        g_print("INFO: Canvas UI built, checking for existing history...\n");
        // 延迟重绘
        g_timeout_add(200, (GSourceFunc)delayed_repaint, app);
    }
}

static void activate(GtkApplication *app, gpointer user_data) {
    AppWidgets *widgets = (AppWidgets *)user_data;

    widgets->window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(widgets->window), "Canvas Client (Linux)");
    gtk_window_set_default_size(GTK_WINDOW(widgets->window), 800, 600);
    
    g_signal_connect(widgets->window, "delete-event", G_CALLBACK(on_window_close), widgets);

    widgets->main_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(widgets->window), widgets->main_container);

    build_login_ui(widgets);
    
    core_init();
    core_set_callbacks(
        on_login_success,
        on_login_failure,
        on_register_success,
        on_register_failure,
        on_room_list,
        on_join_room_success,
        on_save_canvas_success,
        on_save_canvas_failure,
        on_draw,
        on_chat,
        on_undo,
        on_redo,
        on_clear,
        on_load_canvas,
        on_user_joined,
        on_chat_history,
        widgets
    );
    
    if (!widgets->client) {
        widgets->client = net_client_new(widgets->server_ip, widgets->server_port);
        if (widgets->client) {
            net_client_set_callbacks(widgets->client, (net_callback_t)on_net_connect, (net_callback_t)on_net_disconnect, (net_callback_t)on_net_message, widgets);
            core_set_client(widgets->client);
            net_client_connect(widgets->client);
        }
    }
}

int main(int argc, char **argv) {
    GtkApplication *app;
    int status;
    static AppWidgets widgets = {0};
    
    // Defaults
    widgets.state.line_width = 3.0;
    widgets.state.tool_type = TOOL_PEN;

    // 解析命令行参数
    const char *server_ip = "127.0.0.1";  // 默认本地地址
    int server_port = 8080;                // 默认端口
    
    // 简单参数解析：./canvas_client [IP] [PORT]
    if (argc > 1) {
        server_ip = argv[1];
        g_print("INFO: Server IP specified: %s\n", server_ip);
    }
    if (argc > 2) {
        server_port = atoi(argv[2]);
        if (server_port <= 0 || server_port > 65535) {
            g_print("ERROR: Invalid port number: %s, using default 8080\n", argv[2]);
            server_port = 8080;
        } else {
            g_print("INFO: Server port specified: %d\n", server_port);
        }
    }
    
    if (argc == 1) {
        g_print("INFO: No arguments provided, using default server: %s:%d\n", 
               server_ip, server_port);
        g_print("INFO: Usage: %s [IP] [PORT]\n", argv[0]);
    }

    char app_id[64];
    snprintf(app_id, sizeof(app_id), "org.canvas.client.pid%d", getpid());
    app = gtk_application_new(app_id, G_APPLICATION_FLAGS_NONE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), &widgets);
    
    strncpy(widgets.server_ip, server_ip, sizeof(widgets.server_ip) - 1);
    widgets.server_ip[sizeof(widgets.server_ip) - 1] = '\0';
    widgets.server_port = server_port;
    
    status = g_application_run(G_APPLICATION(app), argc, argv);
    
    if (widgets.client) {
        net_client_free(widgets.client);
    }
    g_object_unref(app);

    return status;
}