#include <gtk/gtk.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "network.h"
#include "protocol_ws.h"

struct net_client_s {
    int fd;
    char buffer[4096];
    size_t buf_len;
    int connected;
    int handshake_complete;
    
    net_callback_t on_message;
    net_callback_t on_connect;
    net_callback_t on_disconnect;
    void *user_data;
    
    guint io_watch_id;
};

static gboolean socket_io_callback(GIOChannel *source, GIOCondition condition, gpointer data);

net_client_t *net_client_new(const char *url) {
    (void)url; // URL parsing not implemented, assuming localhost:8080
    net_client_t *client = calloc(1, sizeof(net_client_t));
    client->fd = -1;
    return client;
}

void net_client_set_callbacks(net_client_t *client, 
                              net_callback_t on_connect,
                              net_callback_t on_disconnect,
                              net_callback_t on_message,
                              void *user_data) {
    if (client) {
        client->on_connect = on_connect;
        client->on_disconnect = on_disconnect;
        client->on_message = on_message;
        client->user_data = user_data;
    }
}

int net_client_connect(net_client_t *client) {
    if (!client) return -1;
    
    client->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client->fd == -1) return -1;
    
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(8080);
    
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        close(client->fd);
        return -1;
    }
    
    if (connect(client->fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        close(client->fd);
        return -1;
    }
    
    // Send WebSocket Handshake
    const char *handshake = 
        "GET / HTTP/1.1\r\n"
        "Host: localhost:8080\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";
        
    send(client->fd, handshake, strlen(handshake), 0);
    
    // Set non-blocking for GLib main loop integration
    int flags = fcntl(client->fd, F_GETFL, 0);
    fcntl(client->fd, F_SETFL, flags | O_NONBLOCK);
    
    // Add to GTK main loop
    GIOChannel *channel = g_io_channel_unix_new(client->fd);
    client->io_watch_id = g_io_add_watch(channel, G_IO_IN | G_IO_HUP | G_IO_ERR, 
                                         socket_io_callback, client);
    g_io_channel_unref(channel);
    
    return 0;
}

static gboolean socket_io_callback(GIOChannel *source, GIOCondition condition, gpointer data) {
    (void)source;
    net_client_t *client = (net_client_t *)data;
    
    if (condition & (G_IO_HUP | G_IO_ERR)) {
        if (client->connected) {
            client->connected = 0;
            if (client->on_disconnect) client->on_disconnect(NULL, 0, client->user_data);
        }
        close(client->fd);
        client->fd = -1;
        return FALSE; // Remove source
    }
    
    if (condition & G_IO_IN) {
        ssize_t n = recv(client->fd, client->buffer + client->buf_len, 
                         sizeof(client->buffer) - client->buf_len, 0);
        
        if (n > 0) {
            client->buf_len += n;
            
            if (!client->handshake_complete) {
                // Check for end of header
                char *end_of_header = strstr(client->buffer, "\r\n\r\n");
                if (end_of_header) {
                    client->handshake_complete = 1;
                    client->connected = 1;
                    
                    // Move remaining data to start
                    size_t header_len = (end_of_header - client->buffer) + 4;
                    size_t remaining = client->buf_len - header_len;
                    memmove(client->buffer, client->buffer + header_len, remaining);
                    client->buf_len = remaining;
                    
                    if (client->on_connect) client->on_connect(NULL, 0, client->user_data);
                }
            }
            
            if (client->handshake_complete && client->buf_len > 0) {
                // Parse frames
                char *payload = NULL;
                size_t payload_len = 0;
                int consumed = 0;
                
                while (client->buf_len > 0) {
                    consumed = ws_parse_frame(client->buffer, client->buf_len, &payload, &payload_len);
                    
                    if (consumed == -2) {
                        // Incomplete frame, wait for more data
                        break;
                    } else if (consumed == -1) {
                        // Error, maybe disconnect?
                        // For now just clear buffer to recover
                        client->buf_len = 0;
                        break;
                    }
                    
                    if (consumed > 0) {
                         if (client->on_message) {
                             // Create null-terminated copy for safe processing
                             char *safe_payload = malloc(payload_len + 1);
                             if (safe_payload) {
                                 memcpy(safe_payload, payload, payload_len);
                                 safe_payload[payload_len] = '\0';
                                 client->on_message(safe_payload, payload_len, client->user_data);
                                 free(safe_payload);
                             }
                         }
                         
                         // Move remaining
                         size_t remaining = client->buf_len - consumed;
                         if (remaining > 0) {
                            memmove(client->buffer, client->buffer + consumed, remaining);
                         }
                         client->buf_len = remaining;
                    } else {
                        break;
                    }
                }
            }
        } else {
            // Error or closed
             if (client->connected) {
                client->connected = 0;
                if (client->on_disconnect) client->on_disconnect(NULL, 0, client->user_data);
            }
            return FALSE;
        }
    }
    
    return TRUE;
}

int net_client_send(net_client_t *client, const void *data, size_t len) {
    if (!client || client->fd == -1 || !client->connected) return -1;
    
    char frame[4096];
    
    // Client must mask frames according to spec, but for this custom server simplification
    // we will send UNMASKED frames so the server can just broadcast them as-is.
    
    frame[0] = 0x81; // FIN, Text
    size_t header_len;
    
    if (len < 126) {
        frame[1] = len; // Mask bit NOT set
        header_len = 2; 
    } else if (len < 65536) {
        frame[1] = 126;
        frame[2] = (len >> 8) & 0xFF;
        frame[3] = len & 0xFF;
        header_len = 4;
    } else {
        // ... omitted large frames
        return -1; 
    }
    
    // No Masking
    memcpy(frame + header_len, data, len);
    
    return send(client->fd, frame, header_len + len, 0);
}

void net_client_service(net_client_t *client) {
    // Not needed with GTK main loop integration
    (void)client;
}

void net_client_free(net_client_t *client) {
    if (client) {
        if (client->fd != -1) {
            g_source_remove(client->io_watch_id);
            close(client->fd);
        }
        free(client);
    }
}
