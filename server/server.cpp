#include <algorithm>
#include <arpa/inet.h>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>
using std::mutex;
using std::string;
using std::vector;

#define NUMBER_OF_ROOMS 4

vector<int> clients;

class Player {
public:
  string nickname;
  Player(const string &nickname) : nickname(nickname) {}
};

class Game {
public:
  vector<Player *> players;

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
  Player *player;
};

class Server {
  vector<Room> rooms;
  mutex rooms_mutex;
  vector<Player> players;
  mutex players_mutex;
  vector<Session> sessions;
  int port;
  string ip_address;

  static void handle_client(int client_socket, sockaddr_in client_addr) {
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
    std::cout << "Client connected from " << client_ip << ":"
              << ntohs(client_addr.sin_port) << std::endl;

    char buffer[1024];
    while (true) {
      ssize_t bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
      if (bytes_received <= 0)
        break;
      buffer[bytes_received] = '\0';
      printf("Received: %s\n", buffer);
      // send(client_socket, buffer, bytes_received, 0);
    }
    close(client_socket);
  }

public:
  Server(int port, const string &ip_address)
      : port(port), ip_address(ip_address) {
    // players, sessions, rooms and maps are default-initialized
  }

  int serve() {
    int res;
    printf("Hello World!\n");

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip_address.c_str());

    res = bind(server_socket, (struct sockaddr *)&server_addr,
               sizeof(server_addr));
    if (res) {
      perror("bind error:");
      throw res;
    }

    res = listen(server_socket, 10);
    if (res) {
      perror("listen error:");
      throw res;
    }

    while (true) {
      sockaddr_in client_addr = {};
      socklen_t addrlen = sizeof(client_addr);
      int client_socket = accept(server_socket, (sockaddr *)&client_addr, &addrlen);
      if (client_socket < 0) {
        perror("accept error:");
        continue;
      }
      std::thread(Server::handle_client, client_socket, client_addr).detach();
    }
    return 0;
  }
};

int main(int argc, char **argv) {
  Server *server = new Server(8888, "127.0.0.1");
  server->serve();
  delete server;
}