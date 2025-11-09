Here’s a professional **README.md** file you can directly upload to your GitHub repository for the **System Monitor Tool (C++ project)** you showed in the image:

---

# 🖥️ System Monitor Tool

## 📘 Objective

The **System Monitor Tool** is a C++ application that displays real-time information about system processes, memory usage, and CPU load — similar to the Linux `top` command. It provides users with insights into system performance and allows interaction such as sorting and killing processes.

---

## 🗓️ Day-wise Tasks

### **Day 1:**

* Designed the UI layout.
* Gathered system data using system calls.

### **Day 2:**

* Displayed a process list with CPU and memory usage.

### **Day 3:**

* Implemented process sorting by CPU and memory usage.

### **Day 4:**

* Added functionality to terminate (kill) processes.

### **Day 5:**

* Implemented a real-time data refresh feature to update system information every few seconds.

---

## ⚙️ Features

* Real-time system monitoring
* Displays process ID, CPU %, memory %, and process name
* Sorts processes by CPU or memory usage
* Allows users to terminate unwanted processes
* Auto-refresh system data every few seconds

---

## 🧰 Technologies Used

* **Language:** C++
* **System Calls:** Linux system APIs (`/proc/` filesystem, `sysconf`, etc.)
* **Libraries:**

  * `<iostream>`
  * `<fstream>`
  * `<unistd.h>`
  * `<sys/types.h>`
  * `<dirent.h>`

---

## 🚀 How to Run

1. **Clone the repository:**

   ```bash
   git clone https://github.com/<your-username>/System-Monitor-Tool.git
   cd System-Monitor-Tool
   ```

2. **Compile the code:**

   ```bash
   g++ system_monitor.cpp -o system_monitor
   ```

3. **Run the tool:**

   ```bash
   ./system_monitor
   ```

---

## 📈 Future Enhancements

* Add graphical UI using `ncurses` or Qt.
* Include disk I/O and network statistics.
* Export performance logs to file.

---

## 🧑‍💻 Author

**Uma Patel**
📍 *System Monitor Tool - C++ Project for Operating Systems (Assignment 3 LSP)*

---


