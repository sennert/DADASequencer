
#define MIDI_TIMING_CLOCK 0xF8
#define MIDI_START 0xFA
#define MIDI_STOP 0xFC


void startOrStop() {
  if (!playing) {
    Serial.println("Start playing");
    Serial1.write(MIDI_START);
    uint8_t array[] = {250,};
    usbMIDI.sendSysEx(1, array);
    stepCount = 5;
    blinkCount = 23;
      Timer1.setPeriod(calculateIntervalMicroSecs(bpm));


  } else {
    Serial.println("Stop playing");
    Serial1.write(MIDI_STOP);
    uint8_t array[] = {252,};
    usbMIDI.sendSysEx(1, array);  
    line(col, false);
    analogWrite(SYNC_OUTPUT_PIN, 0);
    analogWrite(BLINK_OUTPUT_PIN, 0);
    col=7;
    line(0, true);    
    refreshTrellis = true;
  }
  playing = !playing;
}

void sendClockPulse() {
  if(playing){
  // Write the timing clock byte
  Serial1.write(MIDI_TIMING_CLOCK);
  uint8_t array[] = {248,};
  usbMIDI.sendSysEx(1, array);

  stepCount = (stepCount+1) % (6);
  if (stepCount == 0)     handleSequencer();

  }
  blinkCount = (blinkCount+1) % CLOCKS_PER_BEAT;
  if (blinkCount == 0) {
    // Turn led on
    analogWrite(BLINK_OUTPUT_PIN, 255);
    // Set sync pin to HIGH
    analogWrite(SYNC_OUTPUT_PIN, 255);
  } else {
    if (blinkCount == 1) {
      // Set sync pin to LOW
      analogWrite(SYNC_OUTPUT_PIN, 0);
    }
    if (blinkCount == BLINK_TIME) {
      // Turn led off
      analogWrite(BLINK_OUTPUT_PIN, 0);
    }
  }
  
  
}
