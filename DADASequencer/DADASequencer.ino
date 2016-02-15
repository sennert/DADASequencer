#include <Wire.h>
#include <Adafruit_Trellis.h>
#include <Adafruit_UNTZtrument.h>
#include <TimerOne.h>
#include <EEPROM.h>

#define START_STOP_INPUT_PIN 3
#define DEBOUNCE_INTERVAL 200L // Milliseconds
#define TAPBUTTON_PIN 5
#define SYNC_OUTPUT_PIN 9 // Can be used to drive sync analog sequencer (Korg Monotribe etc ...)
#define BLINK_OUTPUT_PIN 13
#define BLINK_TIME 4 // How long to keep LED lit in CLOCK counts (so range is [0,24])
#define ENCODER1_PIN1 20
#define ENCODER1_PIN2 21

#define CLOCKS_PER_BEAT 24
#define PRINT_INTERVAL 10000
#define MINIMUM_BPM 40 // Used for debouncing
#define MAXIMUM_BPM 360 // Used for debouncing
#define MINIMUM_TAPS 3
#define EXIT_MARGIN 150 // If no tap after 150% of last tap interval -> measure and set

#define EEPROM_ADDRESS 0 // Where to save BPM

/////////////////////////////////////////////////////////////////
///////TRELLIS INIT/////////////////////////////////////////////
Adafruit_Trellis     T[2];
Adafruit_UNTZtrument untztrument(&T[0], &T[1]);
const uint8_t        addr[] = { 0x70, 0x71};
#define WIDTH     (8)  //#define WIDTH     ((sizeof(T) / sizeof(T[0])) * 2)
#define N_BUTTONS (32)  //#define N_BUTTONS ((sizeof(T) / sizeof(T[0])) * 16)
uint8_t       grid[WIDTH];                 // Sequencer state
uint8_t       col          = WIDTH-1;      // Current column
//unsigned int  bpm          = 240;          // Tempo
unsigned long prevBeatTime = 0L;           // Column step timer
unsigned long prevReadTime = 0L;           // Keypad polling timer

// The note[] and channel[] tables are the MIDI note and channel numbers
// for to each row (top to bottom); they're specific to this application.
// bitmask[] is for efficient reading/writing bits to the grid[] array.
static const uint8_t PROGMEM
  note[8]    = {  42, 38, 37, 36, 39, 38, 37,  36 },
  channel[8] = {   1,  1,  1,  1,  1,  1,  1,   1 },
  bitmask[8] = {   1,  2,  4,  8, 16, 32, 64, 128 };
/////////////////////////////////////////////////////////////////

enc tempoEncoder(ENCODER1_PIN1, ENCODER1_PIN2);

long intervalMicroSeconds;
float bpm = 120.0;

boolean initialized = false;
long minimumTapInterval = 60L * 1000 * 1000 / MAXIMUM_BPM;
long maximumTapInterval = 60L * 1000 * 1000 / MINIMUM_BPM;

volatile long firstTapTime = 0;
volatile long lastTapTime = 0;
volatile long timesTapped = 0;

volatile int blinkCount = 0;

boolean playing = false;
long lastStartStopTime = 0;
boolean refreshTrellis = false;

int stepCount = 0;
float stepLength = 0;

/////////////////////////////////////////////////////////////////////
////////////////////////////SETUP ///////////////////////////////////
void setup() {
  //  Set MIDI baud rate:
  Serial1.begin(31250);
  Serial.begin(9600);
  delay(1000);
  Serial.println("Welcome to DADA Sequencer");

  // Set pin modes
  pinMode(BLINK_OUTPUT_PIN, OUTPUT);
  pinMode(SYNC_OUTPUT_PIN, OUTPUT);
  pinMode(START_STOP_INPUT_PIN, INPUT);
  pinMode(TAPBUTTON_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(TAPBUTTON_PIN), tapInput, RISING); // Interrupt for catching tap events

  untztrument.begin(addr[0], addr[1]);
//#ifdef __AVR__
//  // Default Arduino I2C speed is 100 KHz, but the HT16K33 supports
//  // 400 KHz.  We can force this for faster read & refresh, but may
//  // break compatibility with other I2C devices...so be prepared to
//  // comment this out, or save & restore value as needed.
//  TWBR = 12;
//#endif
  untztrument.clear();
  untztrument.writeDisplay();
  memset(grid, 0, sizeof(grid));
  
  // Get the saved BPM value
  EEPROM.get(EEPROM_ADDRESS, bpm); // We're subtracting 40 when saving to have higher range
  Serial.print("Read BPM from EEPROM: ");
  Serial.println(bpm);

  // Attach the interrupt to send the MIDI clock and start the timer
  Timer1.initialize(intervalMicroSeconds);
  Timer1.setPeriod(calculateIntervalMicroSecs(bpm));
  Timer1.attachInterrupt(sendClockPulse);
  
  // Initialize dimmer value
//  lastDimmerValue = analogRead(DIMMER_INPUT_PIN);
  tempoEncoder.setBounds(60 * 40, 480 * 40 + 39); // Set tempo limits
  tempoEncoder.setValue(bpm * 40);              // *4's for encoder detents
}


/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////
long now = micros();
unsigned long t    = millis();
uint8_t       mask;

void loop() {
  now = micros();
  t    = millis();
  
  updateController();


  /*
   * Handle Trellis Input
   */
  if((t - prevReadTime) >= 20L) { // 20ms = min Trellis poll time
    if(untztrument.readSwitches()) { // Button state change?
      for(uint8_t i=0; i<N_BUTTONS; i++) { // For each button...
        uint8_t x, y;
        untztrument.i2xy(i, &x, &y);
        mask = pgm_read_byte(&bitmask[y]);
        if(untztrument.justPressed(i)) {
          if(grid[x] & mask) { // Already set?  Turn off...
            grid[x] &= ~mask;
            untztrument.clrLED(i);
            usbMIDI.sendNoteOff(pgm_read_byte(&note[y]), 127, pgm_read_byte(&channel[y]));
          } else { // Turn on
            grid[x] |= mask;
            untztrument.setLED(i);
          }
          refreshTrellis = true;
        }
      }
    }
    prevReadTime = t;
//    untztrument.writeDisplay();

  }
  
  if(refreshTrellis) {
    untztrument.writeDisplay();
    refreshTrellis = false;
  }
  while(usbMIDI.read()); // Discard incoming MIDI messages  

}

/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////

void updateController(){
  enc::poll(); // Read encoder(s)
  handleTapButton();
  
  float curEncValue = (tempoEncoder.getValue() / 40.0); // Div for encoder detents
  curEncValue = (int(curEncValue*10))/10.0;
  if(curEncValue != bpm ) {
      bpm = curEncValue;
      updateBpm();
  }

  /*
   * Check for start/stop button pressed
   */
  boolean startStopPressed = digitalRead(START_STOP_INPUT_PIN) > 0 ? true : false;
  if (startStopPressed && (lastStartStopTime+(DEBOUNCE_INTERVAL*1000)) < now) {
    startOrStop();
    lastStartStopTime = now;
  }
}



void handleTapButton(){
    /*
   * Handle tapping of the tap tempo button
   */
  if (timesTapped > 0 && timesTapped < MINIMUM_TAPS && (now - lastTapTime) > maximumTapInterval) {
    // Single taps, not enough to calculate a BPM -> ignore!
//    Serial.println("Ignoring lone taps!");
    timesTapped = 0;
  } else if (timesTapped >= MINIMUM_TAPS) {
    long avgTapInterval = (lastTapTime - firstTapTime) / (timesTapped-1);
    if ((now - lastTapTime) > (avgTapInterval * EXIT_MARGIN / 100)) {
      bpm = 60L * 1000 * 1000 / avgTapInterval;
      
      tempoEncoder.setValue(bpm * 40); 
      updateBpm();

      timesTapped = 0;
    }
  }
}



/*
 * Handle Sequencer
 */
void handleSequencer(){
  if(playing) { // Next beat?
    // Turn off old column
    line(col, false);
    for(uint8_t row=0, mask=1; row<8; row++, mask <<= 1) {
      if(grid[col] & mask) {
        usbMIDI.sendNoteOff(pgm_read_byte(&note[row]), 127, pgm_read_byte(&channel[row]));
      }
    }
    // Advance column counter, wrap around
    if(++col >= WIDTH) col = 0;
    // Turn on new column
    line(col, true);
    for(uint8_t row=0, mask=1; row<8; row++, mask <<= 1) {
      if(grid[col] & mask) {
        usbMIDI.sendNoteOn(pgm_read_byte(&note[row]), 127, pgm_read_byte(&channel[row]));
      }
    }
    refreshTrellis      = true;
  }
}
