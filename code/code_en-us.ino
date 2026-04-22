// Includes the native Arduino library for MIDI communication via USB cable.
// The computer will recognize the board as a musical instrument ("Arduino Leonardo:Arduino Leonardo MIDI 1 20:0").
#include "MIDIUSB.h"

// ==========================================
// PHYSICAL MATRIX CONFIGURATIONS
// ==========================================

// Defines the number of rows and columns in the button matrix.
const int NUM_ROWS = 3;  // 3 electrical rows.
const int NUM_COLS = 17; // 17 electrical columns.

// Calculates the maximum number of buttons the matrix supports (3 rows * 17 columns = 51 keys).
const int NUM_KEYS = 51; 

// Indicates which Arduino pins are connected to the Rows.
// Digital pins 0, 1, and 2.
int rowPins[NUM_ROWS] = {0, 1, 2}; 

// Indicates which Arduino pins are connected to the Columns.
// Digital pins 3 to 13, and analog pins A0 to A5 (used as digital).
int colPins[NUM_COLS] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, A0, A1, A2, A3, A4, A5};

// ==========================================
// STATE VARIABLES AND ANTI-NOISE FILTER (DEBOUNCE)
// ==========================================

// Array (list) that stores the CURRENT state of each key (true = pressed, false = released).
bool keyState[NUM_KEYS];

// Array that stores the LAST read state of each key to compare if there was a change.
bool lastKeyState[NUM_KEYS];

// Array that records the exact millisecond each key last changed state.
unsigned long lastDebounceTime[NUM_KEYS];

// Time in milliseconds that the Arduino must wait to confirm the button was actually
// pressed or released. This prevents the button's internal metallic "bounce" from generating double notes.
unsigned long debounceDelay = 10; 

// ==========================================
// CONTROL VARIABLES (OCTAVES AND SYSTEMS)
// ==========================================

// Stores the current octave shift. Starts at 0. Can go to +12 (high) or -12 (low).
int octaveShift = 0;      

// Defines which note system is active. 
// false = C-System (Initial default), true = B-System.
bool isBSystem = false;   

// Smart memory: Records the exact note sent to the computer when the key was pressed.
// This prevents a note from getting "stuck" playing forever if you change the octave before releasing it.
int notasAtivas[NUM_KEYS];

// Defines the "position" (mathematical index) of the control buttons within the matrix.
// These buttons are all in Column 0, but on different rows.
const int BTN_OCTAVE_DOWN = 0;  // Octave down button (Row 0 * 17 columns + Column 0)
const int BTN_OCTAVE_UP   = 17; // Octave up button (Row 1 * 17 columns + Column 0)
const int BTN_SYSTEM      = 34; // C/B switch button (Row 2 * 17 columns + Column 0)

// ==========================================
// NOTE MAPS (LOOK-UP TABLES)
// ==========================================

// MAP 1: C-SYSTEM (C-Griff) 
// Defines which default MIDI note each button will play. The number 0 indicates a button without a note (like functions).
int noteMapCSystem[NUM_KEYS] = {
  // Row 0 (The first '0' is the BTN_OCTAVE_DOWN button)
  0, 48, 51, 54, 57, 60, 63, 66, 69, 72, 75, 78, 81, 84, 87, 90, 93,
  // Row 1 (The first '0' is the BTN_OCTAVE_UP button)
  0, 49, 52, 55, 58, 61, 64, 67, 70, 73, 76, 79, 82, 85, 88, 91, 94,
  // Row 2 (The first '0' is the BTN_SYSTEM button)
  0, 50, 53, 56, 59, 62, 65, 68, 71, 74, 77, 80, 83, 86, 89, 92, 95
};

// MAP 2: B-SYSTEM (B-Griff)
// Alternative map for when the system switch button is activated.
int noteMapBSystem[NUM_KEYS] = {
  // Row 0
  0, 47, 50, 53, 56, 59, 62, 65, 68, 71, 74, 77, 80, 83, 86, 89, 92,
  // Row 1
  0, 48, 51, 54, 57, 60, 63, 66, 69, 72, 75, 78, 81, 84, 87, 90, 93,
  // Row 2
  0, 49, 52, 55, 58, 61, 64, 67, 70, 73, 76, 79, 82, 85, 88, 91, 94
};

// ==========================================
// SETUP: INITIAL PREPARATION (Runs only once)
// ==========================================
void setup() {
  // Configures all ROW pins to send power (OUTPUT).
  for (int i = 0; i < NUM_ROWS; i++) {
    pinMode(rowPins[i], OUTPUT);
    digitalWrite(rowPins[i], HIGH); // Keep HIGH (5V) so they remain inactive by default.
  }
  
  // Configures all COLUMN pins to receive power.
  // INPUT_PULLUP enables an internal resistor that keeps columns reading 5V (HIGH) when released.
  for (int i = 0; i < NUM_COLS; i++) {
    pinMode(colPins[i], INPUT_PULLUP);
  }
  
  // Clears all keyboard memories and timers to start fresh.
  for (int i = 0; i < NUM_KEYS; i++) {
    keyState[i] = false;
    lastKeyState[i] = false;
    lastDebounceTime[i] = 0;
    notasAtivas[i] = 0; 
  }
}

// ==========================================
// MAIN LOOP (Runs infinitely and as fast as possible)
// ==========================================
void loop() {
  // Calls the function that scans (radars) the keys.
  scanMatrix();
}

// ==========================================
// MATRIX SCANNING FUNCTION
// ==========================================
void scanMatrix() {
  // Loops through each of the 3 rows, one at a time.
  for (int r = 0; r < NUM_ROWS; r++) {
    
    // Activates the current row by dropping its voltage to 0V (LOW).
    digitalWrite(rowPins[r], LOW); 

    // While the row is active (0V), loops through each of the 17 columns checking for contact.
    for (int c = 0; c < NUM_COLS; c++) {
      
      // Calculates the unique identification number for the key (from 0 to 50).
      int keyIndex = r * NUM_COLS + c;

      // Reads the current column. The "!" symbol inverts the reading. 
      // Since pressing the key generates 0V (false), the "!" turns it into "true" for the code.
      bool reading = !digitalRead(colPins[c]); 

      // If the reading is different from the last recorded state (sign that someone touched or released):
      if (reading != lastKeyState[keyIndex]) {
        // Resets the timer for this specific key by recording the current time (millis).
        lastDebounceTime[keyIndex] = millis();
      }

      // Checks if the signal has stabilized longer than the debounceDelay (10ms).
      if ((millis() - lastDebounceTime[keyIndex]) > debounceDelay) {
        
        // If the stable reading is actually different from the official state saved in memory:
        if (reading != keyState[keyIndex]) {
          // Updates the official memory with the new state.
          keyState[keyIndex] = reading;

          // ==========================================
          // LOGIC FOR WHEN THE KEY IS PRESSED
          // ==========================================
          if (keyState[keyIndex]) {
            
            // --- 1. CHECK IF IT IS ONE OF THE FUNCTION BUTTONS ---
            if (keyIndex == BTN_OCTAVE_DOWN || keyIndex == BTN_OCTAVE_UP || keyIndex == BTN_SYSTEM) {
              
              if (keyIndex == BTN_OCTAVE_DOWN) {
                // Decreases 12 semitones with each press. 
                // Limit of -36 (3 octaves) so the user doesn't "get lost" in silence.
                octaveShift -= 12;
                if (octaveShift < -36) octaveShift = -36; 
              } 
              else if (keyIndex == BTN_OCTAVE_UP) {
                // Increases 12 semitones with each press.
                // Limit of 24 (2 octaves)
                octaveShift += 12;
                if (octaveShift > 24) octaveShift = 24;
              } 
              else if (keyIndex == BTN_SYSTEM) {
                // Toggles between C-System and B-System
                isBSystem = !isBSystem;
              }

              // REAL-TIME MAGIC: Updates the sound of notes already being held at this exact moment.
              for (int i = 0; i < NUM_KEYS; i++) {
                // Skips the function buttons themselves (they don't play sound).
                if (i == BTN_OCTAVE_DOWN || i == BTN_OCTAVE_UP || i == BTN_SYSTEM) continue;

                // If there is an active note recorded in memory for button 'i':
                if (notasAtivas[i] > 0) { 
                  // 1. Sends command to turn off the current old note.
                  noteOff(0, notasAtivas[i], 0);
                  
                  // 2. Checks the map for what the base note of this button should be with the current system.
                  int baseNote = isBSystem ? noteMapBSystem[i] : noteMapCSystem[i];
                  
                  if (baseNote > 0) {
                    // Adds the new tuning/octave and uses 'constrain' to ensure it doesn't exceed 127 or go below 0.
                    int finalNote = constrain(baseNote + octaveShift, 0, 127);
                    
                    // 3. Turns on the note in the new octave instantly and saves it in memory.
                    noteOn(0, finalNote, 127);
                    notasAtivas[i] = finalNote;
                  }
                }
              }
              // Pushes accumulated MIDI messages to USB instantly.
              MidiUSB.flush(); 
            } 
            
            // --- 2. IF IT IS A NORMAL MUSICAL NOTE KEY ---
            else {
              // Finds out what the note is by looking at the correct map based on the chosen system (B or C).
              int baseNote = isBSystem ? noteMapBSystem[keyIndex] : noteMapCSystem[keyIndex];

              // Only plays sound if the map doesn't show '0' for that key.
              if (baseNote > 0) {
                // Calculates the final note including the octave shift, without exceeding the 0 to 127 limit.
                int finalNote = constrain(baseNote + octaveShift, 0, 127); 
                
                // Records which note will be played in memory (so we don't get lost when releasing) and turns it on.
                notasAtivas[keyIndex] = finalNote;
                noteOn(0, finalNote, 127);  
                MidiUSB.flush();           
              }
            }
          } 
          
          // ==========================================
          // LOGIC FOR WHEN THE KEY IS RELEASED
          // ==========================================
          else {
            // If the released button is NOT one of the function buttons:
            if (keyIndex != BTN_OCTAVE_DOWN && keyIndex != BTN_OCTAVE_UP && keyIndex != BTN_SYSTEM) {
              
              // Checks memory to know exactly which MIDI note number had been sent by this key.
              if (notasAtivas[keyIndex] > 0) {
                // Turns off exactly that registered note.
                noteOff(0, notasAtivas[keyIndex], 0);   
                MidiUSB.flush();
                // Clears the memory space for this key.
                notasAtivas[keyIndex] = 0; 
              }
            }
          }
        }
      }
      // Updates the old state with the current raw mechanical reading for the next loop iteration.
      lastKeyState[keyIndex] = reading;
    }
    // Before moving to the next matrix row, turns off the current row by returning it to 5V (HIGH).
    digitalWrite(rowPins[r], HIGH); 
  }
}

// ==========================================
// USB MIDI COMMUNICATION FUNCTIONS
// ==========================================

// Function that builds the data packet required by the MIDI specification to turn a note "On" (Note On).
void noteOn(byte channel, byte pitch, byte velocity) {
  // 0x09 is the USB header for "Note On". 0x90 is the MIDI code for "Note On on channel 0".
  midiEventPacket_t noteOn = {0x09, (byte)(0x90 | channel), pitch, velocity};
  // Sends the packet to the USB buffer.
  MidiUSB.sendMIDI(noteOn);
}

// Function that builds the data packet to turn a note "Off" (Note Off).
void noteOff(byte channel, byte pitch, byte velocity) {
  // 0x08 is the USB header for "Note Off". 0x80 is the MIDI code for "Note Off on channel 0".
  midiEventPacket_t noteOff = {0x08, (byte)(0x80 | channel), pitch, velocity};
  // Sends the packet to the USB buffer.
  MidiUSB.sendMIDI(noteOff);
}