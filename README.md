# esp-simplex
This is the arduino code to load into the ESP-01/ESP-01S on a relay board, like lctech.
It will connect to Wifi, sync it's time with NTP and pulse the relay for 8 and 14 seconds to sync
a Simplex "slave" clock, using the clock's sync "clutch".
Wire the clutch to the relay (with one side of the line voltage), 120V to the clock mains,
and supply 5VDC to the relay board.
The hourly sync will happen a few minutes before every hour and the daily sync will happen
at (a couple minutes before) 6AM and 6PM.
