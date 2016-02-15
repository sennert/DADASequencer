
void updateBpm() {
    // Update the timer
  Timer1.setPeriod(calculateIntervalMicroSecs(bpm));
  // Save the BPM
  EEPROM.put(EEPROM_ADDRESS, bpm); // Save with offset 40 to have higher range
  Serial.print("Set BPM to: ");
  Serial.println(bpm);
}

long calculateIntervalMicroSecs(int bpm) {
  // Take care about overflows!
  return 60L * 1000 * 1000 / bpm / CLOCKS_PER_BEAT;
}


void tapInput() {
  long now = micros();
  if (now - lastTapTime < minimumTapInterval) {
    return; // Debounce
  }

  if (timesTapped == 0) {
    firstTapTime = now;
  }

  timesTapped++;
  lastTapTime = now;
  Serial.println("Tap!");
}


// Turn on (or off) one column of the display
void line(uint8_t x, boolean set) {
  for(uint8_t mask=1, y=0; y<8; y++, mask <<= 1) {
    uint8_t i = untztrument.xy2i(x, y);
    if(set || (grid[x] & mask)) untztrument.setLED(i);
    else                        untztrument.clrLED(i);
  }
}
