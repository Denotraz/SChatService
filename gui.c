/* 
    Simple chat client using C sockets and GTK for UI 
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>

#if _WIN32
    #pragma message("Compiling with _WIN32 defined")
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef SOCKET socket_t;
#else
    #pragma message("Compiling WITHOUT _WIN32")
    #include <unistd.h>
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    typedef int socket_t;
#endif

#ifdef _WIN32
static int net_init(void){
    WSADATA wsaData;
    int r = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (r != 0){
        fprintf(stderr,"WSAStartup failed: %d\n",r);
        return -1;
    }
    return 0;
}

static void net_cleanup(void){
    WSACleanup();
}

static void net_close(socket_t s){
    if (s != INVALID_SOCKET){
        closesocket(s);
    }
}

#else
static int net_init(void){
    return 0;
}

static void net_cleanup(void){

}

static void net_close(socket_t s){
    if (s >= 0){
        close(s);
    }
}
#endif


#define SERVER_IP   "207.244.241.177"
#define SERVER_PORT 5000
#define BUF_SIZE    1024
#define NAME_SIZE   32

// Forward declare struct so we can use it in the callback type
typedef struct ChatApp ChatApp;

// Callback type for incoming messages
typedef void (*MessageHandler)(ChatApp *app, const char *msg);

// Define the ChatApp struct
struct ChatApp {
    socket_t sockfd;
    int connected;
    char username[NAME_SIZE];
    MessageHandler on_message;

    // Gtk Widgets
    GtkWidget *chat_view;
    GtkTextBuffer *chat_buffer;
    GtkWidget *entry;
    GtkWidget *window;
};

// Function 
int app_connect_and_join(ChatApp *app, const char *server_ip, int port, const char *username) {
    struct sockaddr_in server_addr;
    char buffer[BUF_SIZE];

    // Create TCP socket
    app->sockfd = socket(AF_INET, SOCK_STREAM, 0);
#ifdef _WIN32
    if (app->sockfd == INVALID_SOCKET) {
        fprintf(stderr, "socket failed: %d\n", WSAGetLastError());
        return -1;
    }
#else
    if (app->sockfd == -1) {
        perror("socket");
        return -1;
    }
#endif

    // Fill server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {

#ifdef _WIN32
        fprintf(stderr, "inet_pton() failed: %d\n", WSAGetLastError());
#else
        perror("inet_pton");
#endif
        net_close(app->sockfd);
        return -1;
    }

    // Connect to server
    if (connect(app->sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
#ifdef _WIN32
        fprintf(stderr, "connect() failed: %d\n", WSAGetLastError());
#else
        perror("connect");
#endif
        net_close(app->sockfd);
        return -1;
    }

    printf("Connected to server %s:%d\n", server_ip, port);

    // Send JOIN username
    snprintf(buffer, sizeof(buffer), "JOIN %s\n", username);
    if (send(app->sockfd, buffer, strlen(buffer), 0) == -1) {
#ifdef _WIN32
        fprintf(stderr, "send() failed: %d\n", WSAGetLastError());
#else
        perror("send JOIN");
#endif
        net_close(app->sockfd);
        return -1;
    }

    app->connected = 1;
    strncpy(app->username, username, NAME_SIZE - 1);
    app->username[NAME_SIZE - 1] = '\0';

    return 0;
}

// Function to send a message to the server
int app_send_message(ChatApp *app, const char *message) {
    if (!app->connected) {
        fprintf(stderr, "Not connected to server.\n");
        return -1;
    }

    ssize_t n = send(app->sockfd, message, strlen(message), 0);
    if (n == -1) {
        perror("send");
        return -1;
    }

    return 0;
}

// Function to receive messages from the server
void app_receive_loop(ChatApp *app) {
    char buffer[BUF_SIZE];

    if (!app->connected) {
        fprintf(stderr, "Not connected; receive loop will not run.\n");
        return;
    }

    // Block and read from the server socket only
    while (app->connected) {
        ssize_t n = recv(app->sockfd, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) {
            // Connection closed or error
            if (n == 0) {
                // Server closed connection
                if (app->on_message) {
                    app->on_message(app, "[server] Connection closed by server.\n");
                } else {
                    printf("[server] Connection closed by server.\n");
                }
            } else {
                perror("recv");
            }

            app->connected = 0;
            break;
        }

        buffer[n] = '\0';

        // Hand the message to whoever is interested (CLI, GUI, etc.)
        if (app->on_message) {
            app->on_message(app, buffer);
        } else {
            // Fallback behavior: print to stdout (CLI-style)
            printf("%s", buffer);
            fflush(stdout);
        }
    }
}

// Function called when Send button is clicked
void on_send_clicked(GtkWidget *widget, gpointer data) {
    ChatApp *app = (ChatApp *)data;

    const char *text = gtk_entry_get_text(GTK_ENTRY(app->entry));
    if (strlen(text) == 0) return;

    char msg[BUF_SIZE];
    snprintf(msg, sizeof(msg), "%s\n", text);

    char local[BUF_SIZE + 16];
    snprintf(local, sizeof(local), "[You] %s\n", text);

    if (app->on_message){
        app->on_message(app, local);
    }

    app_send_message(app, msg);

    gtk_entry_set_text(GTK_ENTRY(app->entry), "");
}

// Function to close the app connection upon exit
void app_close(ChatApp *app) {
    if (app->connected) {
        net_close(app->sockfd);
        app->connected = 0;
    }
}

// Function to handle incoming messages in the GUI
void gui_message_handler(ChatApp *app, const char *msg) {
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(app->chat_buffer, &end);
    gtk_text_buffer_insert(app->chat_buffer, &end, msg, -1);
}

// Function to setup the GTK GUI
void setup_gui(ChatApp *app) {

    // Main window
    app->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app->window), "Chat Client");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 500, 400);

    g_signal_connect(app->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    // Vertical layout
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(app->window), vbox);

    // Chat text view
    app->chat_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(app->chat_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(app->chat_view), FALSE);

    app->chat_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->chat_view));

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), app->chat_view);

    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    // Entry + Send button
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    app->entry = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(hbox), app->entry, TRUE, TRUE, 0);

    GtkWidget *send_button = gtk_button_new_with_label("Send");
    gtk_box_pack_start(GTK_BOX(hbox), send_button, FALSE, FALSE, 0);

    // Signals
    g_signal_connect(send_button, "clicked", G_CALLBACK(on_send_clicked), app);
    g_signal_connect(app->entry, "activate", G_CALLBACK(on_send_clicked), app);

    gtk_widget_show_all(app->window);
}

// Function to prompt user for a username
static gboolean ask_username(GtkWindow *parent, char *out, size_t out_size) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Enter Username",
        parent,
        GTK_DIALOG_MODAL,
        "_OK", GTK_RESPONSE_OK,
        "_Cancel", GTK_RESPONSE_CANCEL,
        NULL
    );

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *entry = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(content), entry, TRUE, TRUE, 5);
    gtk_widget_show_all(dialog);

    gint resp = gtk_dialog_run(GTK_DIALOG(dialog));

    gboolean ok = FALSE;
    if (resp == GTK_RESPONSE_OK) {
        const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
        if (text && *text) {
            strncpy(out, text, out_size - 1);
            out[out_size - 1] = '\0';
            ok = TRUE;
        }
    }

    gtk_widget_destroy(dialog);
    return ok;
}
// Funcrtion to handle socket readability
gboolean socket_readable_cb(GIOChannel *source, GIOCondition cond, gpointer data) {
    ChatApp *app = (ChatApp *)data;
    char buffer[BUF_SIZE];

    if (cond & (G_IO_HUP | G_IO_ERR)) {
        // Connection closed / error
        if (app->on_message) {
            app->on_message(app, "[server] Connection closed.\n");
        }
        app_close(app);
        return FALSE; // remove this watch
    }

    if (cond & G_IO_IN) {
        ssize_t n = recv(app->sockfd, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) {
            if (app->on_message) {
                app->on_message(app, "[server] Connection closed.\n");
            }
            app_close(app);
            return FALSE;
        }

        buffer[n] = '\0';

        if (app->on_message) {
            app->on_message(app, buffer);
        } else {
            printf("%s", buffer);
            fflush(stdout);
        }
    }

    return TRUE; // keep watching
}


int main(int argc, char *argv[]) {
    if (net_init() != 0){
        fprintf(stderr,"Network initialization failed.\n");
        return EXIT_FAILURE;
    }
    gtk_init(&argc, &argv);

    ChatApp app = {0};
    app.on_message = gui_message_handler;

    setup_gui(&app);

    // Ask for username
    char username[NAME_SIZE];
    if (!ask_username(GTK_WINDOW(app.window), username, sizeof(username))) {
        fprintf(stderr, "No username entered, exiting.\n");
        net_cleanup();
        return EXIT_FAILURE;
    }

    // Connect to server and JOIN
    if (app_connect_and_join(&app, SERVER_IP, SERVER_PORT, username) != 0) {
        fprintf(stderr, "Failed to connect and join.\n");
        net_cleanup();
        return EXIT_FAILURE;
    }
#ifdef _WIN32
    GIOChannel *channel = g_io_channel_win32_new_socket(app.sockfd);
#else
    GIOChannel *channel = g_io_channel_unix_new(app.sockfd);
#endif
    g_io_add_watch(channel,G_IO_IN | G_IO_HUP | G_IO_ERR,socket_readable_cb,&app);

    gtk_main();

    app_close(&app);
    net_cleanup();
    return 0;
}
