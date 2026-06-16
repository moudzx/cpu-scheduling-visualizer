# cpu-scheduling-visualizer

## Features
### CPU Scheduler

Supports multiple scheduling algorithms:

- FCFS (First Come First Serve)
- SJF (Shortest Job First) / Preemptive and Non Preemptive
- Round Robin
- Priority Scheduling / Preemptive and Non Preemptive

For each algorithm the application provides:

- Editable process table
- Arrival time
- Burst time
- Priority
- Dynamic process addition/removal
- Round Robin quantum configuration
- Animated execution playback
- Adjustable playback speed
- Gantt chart visualization
- Waiting time calculation
- Turnaround time calculation
- Average waiting time
- Average turnaround time
- Current execution indicator
- Per-process execution timeline

#


https://github.com/user-attachments/assets/8a939f1d-e86b-4df9-b31c-7ac915381471



### Countdown Timer

- Enter any number of seconds
- Large digital clock display
- Start and reset controls
- Progress bar visualization
- Color changes as time runs out
- Flashing "TIME'S UP!" notification
- Uses Unix 'alarm()' and signal handling

<img width="1275" height="501" alt="timer" src="https://github.com/user-attachments/assets/83b7e356-c732-45b7-9056-f8e863ff7766" />


---
## Educational Purpose

This project is suitable for:

- Operating Systems courses
- CPU scheduling demonstrations
- Classroom presentations
- Student projects
- Visual learning of scheduling algorithms

---

## Install Raylib
<pre><code>
sudo apt update
sudo apt install build-essential git cmake pkg-config
  
sudo apt install cmake libgl1-mesa-dev libx11-dev libxrandr-dev libxi-dev libxinerama-dev libxcursor-dev

git clone --depth=1 --branch 5.0 https://github.com/raysan5/raylib.git

cd raylib && mkdir build && cd build

cmake .. -DPLATFORM=Desktop -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX=$HOME/raylib-install

make -j4 && make install
</code></pre>
## Compilation

<code>gcc app.c -o app   -I$HOME/raylib-install/include   -L$HOME/raylib-install/lib   -lraylib -lm -lpthread -ldl -lGL -lX11 -lXrandr -lXi -lXinerama -lXcursor && ./app</code>

or run: <code>bash compilation.sh</code>

## Initial storyboard

[Priority CPU-Scheduling.pptx](https://github.com/user-attachments/files/28986872/Priority.CPU-Scheduling.pptx)


## License

MIT License
