import numpy as np
from sklearn.ensemble import IsolationForest
from sklearn.linear_model import LinearRegression
from collections import deque

class SystemAnalyzer:
    def __init__(self, history_size=100):
        self.history_size = history_size
        self.cpu_history = deque(maxlen=history_size)
        self.ram_history = deque(maxlen=history_size)
        
        # Anomaly detectors
        self.cpu_clf = IsolationForest(contamination=0.1, random_state=42)
        self.ram_clf = IsolationForest(contamination=0.1, random_state=42)
        
        # Training state
        self.is_fitted = False
        self.data_count = 0
        self.train_threshold = 50  # Train after 50 data points

    def update(self, cpu, ram, disk_percent):
        self.cpu_history.append(cpu)
        self.ram_history.append(ram)
        self.data_count += 1
        
        anomaly_cpu = False
        anomaly_ram = False
        predicted_cpu = 0.0
        
        # Train/Update models
        if self.data_count >= self.train_threshold:
            # Reshape for sklearn
            cpu_data = np.array(self.cpu_history).reshape(-1, 1)
            ram_data = np.array(self.ram_history).reshape(-1, 1)
            
            if not self.is_fitted or self.data_count % 50 == 0:
                self.cpu_clf.fit(cpu_data)
                self.ram_clf.fit(ram_data)
                self.is_fitted = True
            
            # Predict anomaly for current point
            # -1 is anomaly, 1 is normal
            # Sensitivity adjustment: Contamination parameter in IF is fixed at init, 
            # but we can adjust decision_function threshold if we wanted more control.
            # For simplicity, we'll re-init if sensitivity changes effectively (not implemented here for perf),
            # or just use the boolean result.
            
            if self.cpu_clf.predict([[cpu]])[0] == -1:
                anomaly_cpu = True
            if self.ram_clf.predict([[ram]])[0] == -1:
                anomaly_ram = True
                
            # Future prediction (Simple Linear Regression on last 20 points)
            if len(self.cpu_history) >= 20:
                last_20_y = np.array(list(self.cpu_history)[-20:]).reshape(-1, 1)
                last_20_x = np.arange(20).reshape(-1, 1)
                lr = LinearRegression()
                lr.fit(last_20_x, last_20_y)
                # Predict next step (index 20)
                predicted_cpu = lr.predict([[20]])[0][0]

        # Generate actionable recommendations
        recommendation = "System is running optimally."
        if disk_percent > 90:
             recommendation = "Low Disk Space detected (C:). Clean up files immediately."
        elif anomaly_cpu or predicted_cpu > 80:
            recommendation = "High CPU load detected/predicted. Consider stopping background services or checking for runaway processes."
        elif anomaly_ram:
            recommendation = "Memory usage anomaly detected. Potential memory leak. Recommend clearing cache or restarting heavy applications."
        elif predicted_cpu > 50 and anomaly_cpu:
            recommendation = "Unusual CPU pattern detected. Run virus scan or check scheduled tasks."
                
        return {
            "anomaly_cpu": anomaly_cpu,
            "anomaly_ram": anomaly_ram,
            "predicted_cpu_next": round(predicted_cpu, 2),
            "recommendation": recommendation
        }

    def get_history_dataframe(self):
        import pandas as pd
        # Ensure equal lengths
        length = min(len(self.cpu_history), len(self.ram_history))
        return pd.DataFrame({
            "cpu_usage": list(self.cpu_history)[-length:],
            "ram_percent": list(self.ram_history)[-length:]
        })
