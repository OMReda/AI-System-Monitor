#include "monitor.h"
#include <stdio.h>
#include <windows.h>
#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")

int main() {
  WSADATA wsa;
  SOCKET s, new_socket;
  struct sockaddr_in server, client;
  int c;
  char buffer[2048];

  printf("Initializing Winsock...\n");
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    printf("Failed. Error Code : %d\n", WSAGetLastError());
    return 1;
  }

  if ((s = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
    printf("Could not create socket : %d\n", WSAGetLastError());
    return 1;
  }

  server.sin_family = AF_INET;
  server.sin_addr.s_addr = INADDR_ANY;
  server.sin_port = htons(5000);

  if (bind(s, (struct sockaddr *)&server, sizeof(server)) == SOCKET_ERROR) {
    printf("Bind failed with error code : %d\n", WSAGetLastError());
    return 1;
  }

  listen(s, 3);

  printf("Waiting for incoming connections on port 5000...\n");

  init_monitoring();

  c = sizeof(struct sockaddr_in);

  // Accept one connection for simplicity in this demo, or loop for multiple
  while ((new_socket = accept(s, (struct sockaddr *)&client, &c)) !=
         INVALID_SOCKET) {
    printf("Connection accepted\n");

    SystemMetrics metrics;

    while (1) {
      get_system_metrics(&metrics);

      // JSON format
      // network stats are cumulative, python side will need to diff them for
      // rate
      sprintf(
          buffer,
          "{\"cpu\": %.2f, \"ram_percent\": %.2f, \"ram_used\": %llu, "
          "\"ram_total\": %llu, \"net_recv\": %llu, \"net_sent\": %llu, "
          "\"disk_percent\": %.2f, \"disk_total\": %llu, \"disk_free\": %llu, "
          "\"bat_pct\": %d, \"bat_plug\": %d, \"bat_exist\": %d, "
          "\"cpu_name\": \"%s\", \"gpu_name1\": \"%s\", \"gpu_name2\": \"%s\", "
          "\"ram_name\": \"%s\", \"disk_name\": \"%s\", \"net_name\": "
          "\"%s\"}\n",
          metrics.cpu_usage, metrics.ram_percent, metrics.ram_used,
          metrics.ram_total, metrics.net_bytes_recv, metrics.net_bytes_sent,
          metrics.disk_percent, metrics.disk_total, metrics.disk_free,
          metrics.battery_percent, metrics.battery_plugged,
          metrics.battery_exists, metrics.cpu_name, metrics.gpu_name1,
          metrics.gpu_name2, metrics.ram_name, metrics.disk_name,
          metrics.net_name);

      if (send(new_socket, buffer, strlen(buffer), 0) < 0) {
        printf("Send failed\n");
        break;
      }

      Sleep(1000); // 1 second update rate
    }
    closesocket(new_socket);
    printf("Client disconnected. Waiting for new connection...\n");
  }

  if (new_socket == INVALID_SOCKET) {
    printf("Accept failed with error code : %d\n", WSAGetLastError());
    return 1;
  }

  closesocket(s);
  WSACleanup();

  return 0;
}
