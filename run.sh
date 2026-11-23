#!/data/data/com.termux/files/usr/bin/bash
export DISPLAY=:0
export GALLIUM_DRIVER=llvmpipe
export WATCH_EMU_SLOW=0

if ! pgrep -x "termux-x11" > /dev/null; then
    termux-x11 :0 &
    sleep 2
fi

cd ~/watch-os
./bin/main
