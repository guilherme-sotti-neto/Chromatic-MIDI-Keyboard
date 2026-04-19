#include "MIDIUSB.h"

// Configuração da Matriz Maximizada
const int NUM_ROWS = 10;
const int NUM_COLS = 10;
const int NUM_KEYS = 100; // Capacidade total máxima para 20 pinos

// Definição de todos os 20 pinos do Arduino Leonardo
int rowPins[NUM_ROWS] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
int colPins[NUM_COLS] = {10, 11, 12, 13, A0, A1, A2, A3, A4, A5};

// Arrays para armazenar o estado das teclas e controle de debounce
bool keyState[NUM_KEYS];
bool lastKeyState[NUM_KEYS];
unsigned long lastDebounceTime[NUM_KEYS];
unsigned long debounceDelay = 10; 

// Nota MIDI inicial (21 = A0, a primeira tecla de um piano de 88 teclas)
int baseNote = 21; 

void setup() {
  // Configura as linhas como saída e deixa em HIGH (inativo)
  for (int i = 0; i < NUM_ROWS; i++) {
    pinMode(rowPins[i], OUTPUT);
    digitalWrite(rowPins[i], HIGH); 
  }
  
  // Configura as colunas como entrada com Pull-Up interno
  for (int i = 0; i < NUM_COLS; i++) {
    pinMode(colPins[i], INPUT_PULLUP);
  }
  
  // Inicializa os arrays
  for (int i = 0; i < NUM_KEYS; i++) {
    keyState[i] = false;
    lastKeyState[i] = false;
    lastDebounceTime[i] = 0;
  }
}

void loop() {
  scanMatrix();
}

void scanMatrix() {
  for (int r = 0; r < NUM_ROWS; r++) {
    // Ativa a linha atual colocando-a em LOW
    digitalWrite(rowPins[r], LOW); 

    for (int c = 0; c < NUM_COLS; c++) {
      int keyIndex = r * NUM_COLS + c;

      // Ignora índices que passem da quantidade definida (embora aqui seja exatamente 100)
      if (keyIndex >= NUM_KEYS) continue;

      // Lê o estado da coluna
      bool reading = !digitalRead(colPins[c]); 

      // Lógica de Debounce
      if (reading != lastKeyState[keyIndex]) {
        lastDebounceTime[keyIndex] = millis();
      }

      if ((millis() - lastDebounceTime[keyIndex]) > debounceDelay) {
        if (reading != keyState[keyIndex]) {
          keyState[keyIndex] = reading;

          // Calcula a nota baseada no índice
          // O MIDI suporta notas até 127. Como começamos no 21 e temos 100 teclas, 
          // a nota máxima será 120 (perfeitamente dentro do padrão MIDI).
          int midiNote = baseNote + keyIndex;

          if (keyState[keyIndex]) {
            noteOn(0, midiNote, 127);  
            MidiUSB.flush();           
          } else {
            noteOff(0, midiNote, 0);   
            MidiUSB.flush();
          }
        }
      }
      lastKeyState[keyIndex] = reading;
    }
    // Desativa a linha atual colocando-a de volta em HIGH
    digitalWrite(rowPins[r], HIGH); 
  }
}

// Funções de comunicação MIDI USB
void noteOn(byte channel, byte pitch, byte velocity) {
  midiEventPacket_t noteOn = {0x09, (byte)(0x90 | channel), pitch, velocity};
  MidiUSB.sendMIDI(noteOn);
}

void noteOff(byte channel, byte pitch, byte velocity) {
  midiEventPacket_t noteOff = {0x08, (byte)(0x80 | channel), pitch, velocity};
  MidiUSB.sendMIDI(noteOff);
}