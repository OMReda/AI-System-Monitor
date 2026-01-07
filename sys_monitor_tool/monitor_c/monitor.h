#ifndef MONITOR_H
#define MONITOR_H

#include <stdio.h>
#include <windows.h>

typedef struct {
  double cpu_usage;
  unsigned long long ram_total;
  unsigned long long ram_used;
  unsigned long long ram_free;
  double ram_percent;
  unsigned long long net_bytes_sent;
  unsigned long long net_bytes_recv;
  unsigned long long disk_total;
  unsigned long long disk_free;
  double disk_percent;
  // Battery
  int battery_percent; /* 0-100, or 255 if unknown */
  int battery_plugged; /* 1=AC, 0=Battery */
  int battery_exists;  /* 1=Yes, 0=No/Desktop */
  // Component names
  char cpu_name[128];
  char gpu_name1[128];
  char gpu_name2[128];
  char ram_name[128];
  char disk_name[128];
  char net_name[128];
} SystemMetrics;

// Initialize monitoring (PDH counters etc if needed)
void init_monitoring();

// Get current snapshot of system metrics
void get_system_metrics(SystemMetrics *metrics);

// Get battery status
void get_battery_usage(SystemMetrics *metrics);

#endif // MONITOR_H
