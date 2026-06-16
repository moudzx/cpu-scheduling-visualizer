gcc app.c -o app \
  -I$HOME/raylib-install/include \
  -L$HOME/raylib-install/lib \
  -lraylib -lm -lpthread -ldl -lGL -lX11 -lXrandr -lXi -lXinerama -lXcursor
./app
