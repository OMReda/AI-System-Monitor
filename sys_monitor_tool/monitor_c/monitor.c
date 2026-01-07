#include "monitor.h"
#include <iphlpapi.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#pragma comment(lib, "iphlpapi.lib")

static ULARGE_INTEGER last_idle, last_kernel, last_user;
static int cpu_initialized = 0;
// Helper: Remove jargon and clean spaces
void clean_string(char *str) {
  // Remove "DCH Driver", "Miniport", "Virtual Adapter" etc.
  const char *removals[] = {" DCH Driver",
                            " Miniport",
                            " Virtual Adapter",
                            " PCI Express",
                            " Gigabit Ethernet Controller",
                            " Controller",
                            " 6GB",
                            " 8GB",
                            " 12GB",
                            " 16GB",
                            " 24GB",
                            NULL};
  for (int i = 0; removals[i]; i++) {
    char *found = strstr(str, removals[i]);
    if (found) {
      // Just cut if off? Or remove usage? Let's just remove the substring by
      // shifting
      *found = '\0'; // Simple truncation for now, often these are at end or we
                     // can just cut.
      // Better strategy: replace with empty if in middle, but usually suffixes.
      // For "Gigabit Ethernet Controller", we might want "Gigabit Ethernet".
      // Let's being simple: truncate at the first technical suffix found for
      // cleaner names.
    }
  }

  // Trim trailing spaces
  size_t len = strlen(str);
  while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\r' ||
                     str[len - 1] == '\n')) {
    str[len - 1] = '\0';
    len--;
  }
}

void init_monitoring() {
  FILETIME idle, kernel, user;
  GetSystemTimes(&idle, &kernel, &user);

  last_idle.LowPart = idle.dwLowDateTime;
  last_idle.HighPart = idle.dwHighDateTime;

  last_kernel.LowPart = kernel.dwLowDateTime;
  last_kernel.HighPart = kernel.dwHighDateTime;

  last_user.LowPart = user.dwLowDateTime;
  last_user.HighPart = user.dwHighDateTime;

  cpu_initialized = 1;
}

double get_cpu_usage() {
  FILETIME idle, kernel, user;
  if (!GetSystemTimes(&idle, &kernel, &user))
    return 0.0;

  ULARGE_INTEGER now_idle, now_kernel, now_user;
  now_idle.LowPart = idle.dwLowDateTime;
  now_idle.HighPart = idle.dwHighDateTime;

  now_kernel.LowPart = kernel.dwLowDateTime;
  now_kernel.HighPart = kernel.dwHighDateTime;

  now_user.LowPart = user.dwLowDateTime;
  now_user.HighPart = user.dwHighDateTime;

  ULONGLONG diff_idle = now_idle.QuadPart - last_idle.QuadPart;
  ULONGLONG diff_kernel = now_kernel.QuadPart - last_kernel.QuadPart;
  ULONGLONG diff_user = now_user.QuadPart - last_user.QuadPart;

  ULONGLONG total = diff_kernel + diff_user;
  double percent = 0.0;

  if (total > 0) {
    percent = (double)(total - diff_idle) / total * 100.0;
  }

  last_idle = now_idle;
  last_kernel = now_kernel;
  last_user = now_user;

  return percent;
}

void get_memory_usage(SystemMetrics *metrics) {
  MEMORYSTATUSEX statex;
  statex.dwLength = sizeof(statex);
  GlobalMemoryStatusEx(&statex);

  metrics->ram_total = statex.ullTotalPhys;
  metrics->ram_used = statex.ullTotalPhys - statex.ullAvailPhys;
  metrics->ram_free = statex.ullAvailPhys;
  metrics->ram_percent = (double)statex.dwMemoryLoad;
}

void get_network_usage(SystemMetrics *metrics) {
  DWORD dwSize = 0;
  GetIfTable(NULL, &dwSize, 0);
  PMIB_IFTABLE pIfTable = (PMIB_IFTABLE)malloc(dwSize);

  metrics->net_bytes_recv = 0;
  metrics->net_bytes_sent = 0;

  if (GetIfTable(pIfTable, &dwSize, 0) == NO_ERROR) {
    for (DWORD i = 0; i < pIfTable->dwNumEntries; i++) {
      MIB_IFROW row = pIfTable->table[i];
      if (row.dwOperStatus == MIB_IF_OPER_STATUS_OPERATIONAL) {
        metrics->net_bytes_recv += row.dwInOctets;
        metrics->net_bytes_sent += row.dwOutOctets;
      }
    }
  }
  free(pIfTable);
}

void get_disk_usage(SystemMetrics *metrics) {
  ULARGE_INTEGER freeBytesAvailable, totalNumberOfBytes, totalNumberOfFreeBytes;
  if (GetDiskFreeSpaceEx("C:\\", &freeBytesAvailable, &totalNumberOfBytes,
                         &totalNumberOfFreeBytes)) {
    metrics->disk_total = totalNumberOfBytes.QuadPart;
    metrics->disk_free = totalNumberOfFreeBytes.QuadPart;
    if (metrics->disk_total > 0) {
      unsigned long long used = metrics->disk_total - metrics->disk_free;
      metrics->disk_percent =
          (double)used / (double)metrics->disk_total * 100.0;
    } else {
      metrics->disk_percent = 0.0;
    }
  } else {
    metrics->disk_total = 0;
    metrics->disk_free = 0;
    metrics->disk_percent = 0.0;
  }
}

void get_battery_usage(SystemMetrics *metrics) {
  SYSTEM_POWER_STATUS sps;
  if (GetSystemPowerStatus(&sps)) {
    metrics->battery_percent = sps.BatteryLifePercent;
    metrics->battery_plugged = (sps.ACLineStatus == 1) ? 1 : 0;
    metrics->battery_exists =
        (sps.BatteryFlag != 128) ? 1 : 0; // 128 = No system battery
  } else {
    metrics->battery_percent = 255;
    metrics->battery_plugged = 1; // Assume plugged if unknown
    metrics->battery_exists = 0;
  }
}

void get_cpu_name(char *buffer, size_t size) {
  HKEY hKey;
  DWORD bufferSize = (DWORD)size;
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                    "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0,
                    KEY_READ, &hKey) == ERROR_SUCCESS) {
    if (RegQueryValueExA(hKey, "ProcessorNameString", NULL, NULL,
                         (LPBYTE)buffer, &bufferSize) != ERROR_SUCCESS) {
      strncpy(buffer, "Unknown CPU", size);
    }
    RegCloseKey(hKey);
  } else {
    strncpy(buffer, "Unknown CPU", size);
  }
}

void get_gpu_names(char *buffer1, char *buffer2, size_t size) {
  HKEY hKey;
  char found_names[2][128];
  int count = 0;

  strncpy(buffer1, "Unknown GPU", size);
  strncpy(buffer2, "-", size);

  // Iterate to find all unique GPUs
  for (int i = 0; i < 20 && count < 2; i++) {
    char keyPath[256];
    snprintf(keyPath, sizeof(keyPath),
             "SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-"
             "bfc1-08002be10318}\\%04d",
             i);

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, keyPath, 0, KEY_READ, &hKey) ==
        ERROR_SUCCESS) {
      char tempName[128] = {0};
      DWORD bufSize = sizeof(tempName);
      if (RegQueryValueExA(hKey, "DriverDesc", NULL, NULL, (LPBYTE)tempName,
                           &bufSize) == ERROR_SUCCESS) {
        clean_string(tempName);
        // Check if already found
        int already_have = 0;
        for (int j = 0; j < count; j++) {
          if (strcmp(found_names[j], tempName) == 0) {
            already_have = 1;
            break;
          }
        }
        if (!already_have && strlen(tempName) > 0) {
          strncpy(found_names[count], tempName, 127);
          found_names[count][127] = '\0';
          count++;
        }
      }
      RegCloseKey(hKey);
    }
  }

  if (count >= 1)
    strncpy(buffer1, found_names[0], size);
  if (count >= 2)
    strncpy(buffer2, found_names[1], size);
}

void get_disk_name(char *buffer, size_t size) {
  HKEY hKey;
  char deviceId[256] = {0};
  DWORD deviceIdSize = sizeof(deviceId) - 1;

  // 1. Get the device ID from disk enum
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                    "SYSTEM\\CurrentControlSet\\Services\\disk\\Enum", 0,
                    KEY_READ, &hKey) == ERROR_SUCCESS) {
    if (RegQueryValueExA(hKey, "0", NULL, NULL, (LPBYTE)deviceId,
                         &deviceIdSize) == ERROR_SUCCESS) {
      RegCloseKey(hKey);
      deviceId[deviceIdSize] = '\0'; // Ensure termination

      // 2. Look up the FriendlyName for this device ID
      char enumPath[512];
      snprintf(enumPath, sizeof(enumPath),
               "SYSTEM\\CurrentControlSet\\Enum\\%s", deviceId);
      if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, enumPath, 0, KEY_READ, &hKey) ==
          ERROR_SUCCESS) {
        DWORD bufferSize = (DWORD)size - 1;
        if (RegQueryValueExA(hKey, "FriendlyName", NULL, NULL, (LPBYTE)buffer,
                             &bufferSize) == ERROR_SUCCESS) {
          buffer[bufferSize] = '\0';
          RegCloseKey(hKey);
          return;
        }
        RegCloseKey(hKey);
      }
    } else {
      RegCloseKey(hKey);
    }
  }
  strncpy(buffer, "Local Disk", size);
}

void get_net_name(char *buffer, size_t size) {
  HKEY hKey;
  char best_name[128] = "Network Adapter";
  int found_good = 0;

  for (int i = 0; i < 25; i++) {
    char keyPath[256];
    snprintf(keyPath, sizeof(keyPath),
             "SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e972-e325-11ce-"
             "bfc1-08002be10318}\\%04d",
             i);

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, keyPath, 0, KEY_READ, &hKey) ==
        ERROR_SUCCESS) {
      char tempName[128] = {0};
      DWORD bufSize = sizeof(tempName);
      if (RegQueryValueExA(hKey, "DriverDesc", NULL, NULL, (LPBYTE)tempName,
                           &bufSize) == ERROR_SUCCESS) {
        // Filter out trash
        if (strstr(tempName, "Virtual") || strstr(tempName, "Miniport") ||
            strstr(tempName, "Pseudo") || strstr(tempName, "Kernel") ||
            strstr(tempName, "Loopback")) {
          RegCloseKey(hKey);
          continue;
        }

        // Prioritize WiFi/Ethernet
        int is_good =
            (strstr(tempName, "Wi-Fi") || strstr(tempName, "Wireless") ||
             strstr(tempName, "Ethernet") || strstr(tempName, "Killer") ||
             strstr(tempName, "Intel"));

        if (is_good && !found_good) {
          strncpy(best_name, tempName, sizeof(best_name) - 1);
          found_good = 1;
        } else if (!found_good && strcmp(best_name, "Network Adapter") == 0) {
          strncpy(best_name, tempName, sizeof(best_name) - 1);
        }
      }
      RegCloseKey(hKey);
    }
  }
  strncpy(buffer, best_name, size);
  clean_string(buffer);
}

void get_ram_name(char *buffer, size_t size) {
  MEMORYSTATUSEX statex;
  statex.dwLength = sizeof(statex);
  if (GlobalMemoryStatusEx(&statex)) {
    double totalGB = (double)statex.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
    // If integer (e.g. 16.0), show 16. Else 16.1
    if ((totalGB - (int)totalGB) < 0.1)
      snprintf(buffer, size, "%d GB Memory", (int)totalGB);
    else
      snprintf(buffer, size, "%.1f GB Memory", totalGB);
  } else {
    strncpy(buffer, "System Memory", size);
  }
}

// Static storage for names (fetched once)
static char s_cpu_name[128] = {0};
static char s_gpu_name1[128] = {0};
static char s_gpu_name2[128] = {0};
static char s_ram_name[128] = {0};
static char s_disk_name[128] = {0};
static char s_net_name[128] = {0};
static int names_initialized = 0;

void get_system_metrics(SystemMetrics *metrics) {
  if (!cpu_initialized)
    init_monitoring();

  // Fetch component names once
  if (!names_initialized) {
    get_cpu_name(s_cpu_name, sizeof(s_cpu_name));
    get_gpu_names(s_gpu_name1, s_gpu_name2, sizeof(s_gpu_name1));
    get_ram_name(s_ram_name, sizeof(s_ram_name));
    get_disk_name(s_disk_name, sizeof(s_disk_name));
    get_net_name(s_net_name, sizeof(s_net_name));
    names_initialized = 1;
  }

  // Initialize all to zero to prevent garbage if functions fail
  metrics->cpu_usage = 0;
  metrics->ram_percent = 0;
  metrics->disk_percent = 0;

  metrics->cpu_usage = get_cpu_usage();
  get_memory_usage(metrics);
  get_network_usage(metrics);
  get_disk_usage(metrics);
  get_battery_usage(metrics);

  // Copy names
  strncpy(metrics->cpu_name, s_cpu_name, sizeof(metrics->cpu_name) - 1);
  strncpy(metrics->gpu_name1, s_gpu_name1, sizeof(metrics->gpu_name1) - 1);
  strncpy(metrics->gpu_name2, s_gpu_name2, sizeof(metrics->gpu_name2) - 1);
  strncpy(metrics->ram_name, s_ram_name, sizeof(metrics->ram_name) - 1);
  strncpy(metrics->disk_name, s_disk_name, sizeof(metrics->disk_name) - 1);
  strncpy(metrics->net_name, s_net_name, sizeof(metrics->net_name) - 1);
}
