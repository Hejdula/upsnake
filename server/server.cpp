#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <cstdio>
#include <mutex>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unordered_map>
#include <vector>
using std::array;
using std::string;
using std::vector;
using std::mutex;

#define NUMBER_OF_ROOMS 4

vector<int> clients;

class Player {
public:
  string nickname;
  Player(const string &nickname) : nickname(nickname) {}
};

class Game {
public: 
  vector<Player*> players;

  int tick();
};

class Room {
public:
  vector<Player *> players;
  Game game;
  bool active;
};

class Session {
  int socket;
  Player* player;
};


class Server {
  array<Room *, NUMBER_OF_ROOMS> rooms;
  mutex rooms_mutex;
  vector<Player> players;
  mutex players_mutex;
  vector<Session> sessions;
  int port;
  string ip_address;
public:
  Server(int port, const string& ip_address)
    : port(port), ip_address(ip_address) {
    // Initialize rooms to nullptr or new Room objects as needed
    for (auto& room : rooms) {
      room = nullptr;
    }
    // players, sessions, and maps are default-initialized
  }


  int serve() {
    printf("Hello World!\n");

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip_address.c_str());

    int bind_err = bind(server_socket, (struct sockaddr *)&server_addr,
                        sizeof(server_addr));
    if (bind_err) {
      perror("bind error:");
      return 0;
    }

    int listen_err = listen(server_socket, 10);
    if (bind_err) {
      perror("listen error:");
      return 0;
    }

    sockaddr client_addr;
    socklen_t addrlen;
    int client_socket = accept(server_socket, &client_addr, &addrlen);

    char buffer[1024];
    while (true) {
      ssize_t bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
      if (bytes_received <= 0)
        break; // client closed or error
      buffer[bytes_received] = '\0';
      printf("Received: %s\n", buffer);
      // Optionally, send a response:
      // send(client_socket, buffer, bytes_received, 0);
    }

    return 0;
  };
};

int main(int argc, char **argv) {
  Server *server = new Server(8888, "127.0.0.1");
  server->serve();
}