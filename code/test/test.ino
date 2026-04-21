// Inclui a biblioteca nativa do Arduino para comunicação MIDI via cabo USB.
// O computador reconhecerá a placa como um instrumento musical ("Arduino Leonardo:Arduino Leonardo MIDI 1 20:0").
#include "MIDIUSB.h"

// ==========================================
// CONFIGURAÇÕES DA MATRIZ FÍSICA
// ==========================================

// Define a quantidade de linhas e colunas da matriz de botões.
const int NUM_ROWS = 3;  // Teremos 3 linhas elétricas.
const int NUM_COLS = 17; // Teremos 17 colunas elétricas.

// Calcula o número máximo de botões que a matriz suporta (3 linhas * 17 colunas = 51 teclas).
const int NUM_KEYS = 51; 

// Indica quais pinos do Arduino estão conectados nas Linhas.
// Pinos digitais 0, 1 e 2.
int rowPins[NUM_ROWS] = {0, 1, 2}; 

// Indica quais pinos do Arduino estão conectados nas Colunas.
// Pinos digitais do 3 ao 13, e os pinos analógicos A0 ao A5 (usados como digitais).
int colPins[NUM_COLS] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, A0, A1, A2, A3, A4, A5};

// ==========================================
// VARIÁVEIS DE ESTADO E FILTRO ANTI-RUÍDO (DEBOUNCE)
// ==========================================

// Array (lista) que guarda o estado ATUAL de cada tecla (true = pressionada, false = solta).
bool keyState[NUM_KEYS];

// Array que guarda o ÚLTIMO estado lido de cada tecla para comparar se houve mudança.
bool lastKeyState[NUM_KEYS];

// Array que registra o exato milissegundo em que cada tecla mudou de estado pela última vez.
unsigned long lastDebounceTime[NUM_KEYS];

// Tempo em milissegundos que o Arduino deve esperar para confirmar que o botão foi realmente
// apertado ou solto. Isso evita que o "quique" metálico interno do botão gere notas duplas.
unsigned long debounceDelay = 10; 

// ==========================================
// VARIÁVEIS DE CONTROLE (OITAVAS E SISTEMAS)
// ==========================================

// Guarda o deslocamento atual da oitava. Começa em 0. Pode ir para +12 (agudo) ou -12 (grave).
int octaveShift = 0;      

// Define qual sistema de notas está ativo. 
// false = Sistema C (Padrão inicial), true = Sistema B.
bool isBSystem = false;   

// Memória inteligente: Anota a nota exata que foi enviada ao computador quando a tecla foi pressionada.
// Isso impede que uma nota fique "travada" tocando para sempre se você mudar a oitava antes de soltá-la.
int notasAtivas[NUM_KEYS];

// Define a "posição" (índice matemático) dos botões de controle dentro da matriz.
// Estes botões estão todos na Coluna 0, mas em linhas diferentes.
const int BTN_OCTAVE_DOWN = 0;  // Botão de oitava abaixo (Linha 0 * 17 colunas + Coluna 0)
const int BTN_OCTAVE_UP   = 17; // Botão de oitava acima (Linha 1 * 17 colunas + Coluna 0)
const int BTN_SYSTEM      = 34; // Botão de trocar C/B (Linha 2 * 17 colunas + Coluna 0)

// ==========================================
// MAPAS DE NOTAS (LOOK-UP TABLES)
// ==========================================

// MAPA 1: SISTEMA C (C-Griff) 
// Define qual nota MIDI padrão cada botão vai tocar. O número 0 indica botão sem nota (como as funções).
int noteMapCSystem[NUM_KEYS] = {
  // Linha 0 (O primeiro '0' é o botão BTN_OCTAVE_DOWN)
  0, 48, 51, 54, 57, 60, 63, 66, 69, 72, 75, 78, 81, 84, 87, 90, 93,
  // Linha 1 (O primeiro '0' é o botão BTN_OCTAVE_UP)
  0, 49, 52, 55, 58, 61, 64, 67, 70, 73, 76, 79, 82, 85, 88, 91, 94,
  // Linha 2 (O primeiro '0' é o botão BTN_SYSTEM)
  0, 50, 53, 56, 59, 62, 65, 68, 71, 74, 77, 80, 83, 86, 89, 92, 95
};

// MAPA 2: SISTEMA B (B-Griff)
// Mapa alternativo para quando o botão de trocar de sistema for ativado.
int noteMapBSystem[NUM_KEYS] = {
  // Linha 0
  0, 47, 50, 53, 56, 59, 62, 65, 68, 71, 74, 77, 80, 83, 86, 89, 92,
  // Linha 1
  0, 48, 51, 54, 57, 60, 63, 66, 69, 72, 75, 78, 81, 84, 87, 90, 93,
  // Linha 2
  0, 49, 52, 55, 58, 61, 64, 67, 70, 73, 76, 79, 82, 85, 88, 91, 94
};

// ==========================================
// SETUP: PREPARAÇÃO INICIAL (Roda apenas uma vez)
// ==========================================
void setup() {
  // Configura todos os pinos das LINHAS para enviar energia (OUTPUT).
  for (int i = 0; i < NUM_ROWS; i++) {
    pinMode(rowPins[i], OUTPUT);
    digitalWrite(rowPins[i], HIGH); // Deixa em HIGH (5V) para que fiquem inativos por padrão.
  }
  
  // Configura todos os pinos das COLUNAS para receber energia.
  // O INPUT_PULLUP liga um resistor interno que mantém as colunas lendo 5V (HIGH) quando soltas.
  for (int i = 0; i < NUM_COLS; i++) {
    pinMode(colPins[i], INPUT_PULLUP);
  }
  
  // Limpa todas as memórias e cronômetros do teclado para iniciar zerado.
  for (int i = 0; i < NUM_KEYS; i++) {
    keyState[i] = false;
    lastKeyState[i] = false;
    lastDebounceTime[i] = 0;
    notasAtivas[i] = 0; 
  }
}

// ==========================================
// LOOP PRINCIPAL (Roda infinitamente e o mais rápido possível)
// ==========================================
void loop() {
  // Chama a função que faz a varredura (radar) das teclas.
  scanMatrix();
}

// ==========================================
// FUNÇÃO DE VARREDURA DA MATRIZ
// ==========================================
void scanMatrix() {
  // Passa por cada uma das 3 linhas, uma de cada vez.
  for (int r = 0; r < NUM_ROWS; r++) {
    
    // Ativa a linha atual derrubando sua tensão para 0V (LOW).
    digitalWrite(rowPins[r], LOW); 

    // Enquanto a linha está ativa (0V), passa por cada uma das 17 colunas verificando se há contato.
    for (int c = 0; c < NUM_COLS; c++) {
      
      // Calcula o número de identificação único da tecla (de 0 a 50).
      int keyIndex = r * NUM_COLS + c;

      // Lê a coluna atual. O símbolo "!" inverte a leitura. 
      // Como apertar a tecla gera 0V (falso), o "!" transforma isso em "verdadeiro" para o código.
      bool reading = !digitalRead(colPins[c]); 

      // Se a leitura for diferente do último estado registrado (sinal que alguém tocou ou soltou):
      if (reading != lastKeyState[keyIndex]) {
        // Zera o cronômetro desta tecla específica gravando o tempo atual (millis).
        lastDebounceTime[keyIndex] = millis();
      }

      // Verifica se o sinal já se estabilizou por mais tempo que o debounceDelay (10ms).
      if ((millis() - lastDebounceTime[keyIndex]) > debounceDelay) {
        
        // Se a leitura estável for realmente diferente do estado oficial salvo na memória:
        if (reading != keyState[keyIndex]) {
          // Atualiza a memória oficial com o novo estado.
          keyState[keyIndex] = reading;

          // ==========================================
          // LÓGICA DE QUANDO A TECLA É PRESSIONADA
          // ==========================================
          if (keyState[keyIndex]) {
            
            // --- 1. VERIFICA SE É UM DOS BOTÕES DE FUNÇÃO ---
            if (keyIndex == BTN_OCTAVE_DOWN || keyIndex == BTN_OCTAVE_UP || keyIndex == BTN_SYSTEM) {
              
              if (keyIndex == BTN_OCTAVE_DOWN) {
                // Diminui 12 semitons a cada pressão. 
                // Colocamos um limite de -48 (4 oitavas) para o usuário não "se perder" no silêncio.
                octaveShift -= 12;
                if (octaveShift < -36) octaveShift = -36; 
              } 
              else if (keyIndex == BTN_OCTAVE_UP) {
                // Aumenta 12 semitons a cada pressão.
                octaveShift += 12;
                if (octaveShift > 36) octaveShift = 36;
              } 
              else if (keyIndex == BTN_SYSTEM) {
                // Alterna entre C-System e B-System
                isBSystem = !isBSystem;
              }

              // MÁGICA EM TEMPO REAL: Atualiza o som das notas que já estão sendo seguradas neste exato momento.
              for (int i = 0; i < NUM_KEYS; i++) {
                // Pula os próprios botões de função (eles não tocam som).
                if (i == BTN_OCTAVE_DOWN || i == BTN_OCTAVE_UP || i == BTN_SYSTEM) continue;

                // Se houver uma nota ativa registrada na memória para o botão 'i':
                if (notasAtivas[i] > 0) { 
                  // 1. Envia comando para desligar a nota velha atual.
                  noteOff(0, notasAtivas[i], 0);
                  
                  // 2. Consulta no mapa qual deveria ser a nota base deste botão com o sistema atual.
                  int baseNote = isBSystem ? noteMapBSystem[i] : noteMapCSystem[i];
                  
                  if (baseNote > 0) {
                    // Soma a nova afinação/oitava e usa 'constrain' para garantir que não passe de 127 nem seja menor que 0.
                    int finalNote = constrain(baseNote + octaveShift, 0, 127);
                    
                    // 3. Liga a nota na nova oitava instantaneamente e salva na memória.
                    noteOn(0, finalNote, 127);
                    notasAtivas[i] = finalNote;
                  }
                }
              }
              // Empurra as mensagens MIDI acumuladas para o USB instantaneamente.
              MidiUSB.flush(); 
            } 
            
            // --- 2. SE FOR UMA TECLA DE NOTA MUSICAL NORMAL ---
            else {
              // Descobre qual é a nota olhando o mapa correto com base no sistema escolhido (B ou C).
              int baseNote = isBSystem ? noteMapBSystem[keyIndex] : noteMapCSystem[keyIndex];

              // Só toca som se o mapa não mostrar '0' para aquela tecla.
              if (baseNote > 0) {
                // Calcula a nota final incluindo a mudança de oitava, sem estourar o limite de 0 a 127.
                int finalNote = constrain(baseNote + octaveShift, 0, 127); 
                
                // Grava qual nota vai ser tocada na memória (para não nos perdermos na hora de soltar) e a liga.
                notasAtivas[keyIndex] = finalNote;
                noteOn(0, finalNote, 127);  
                MidiUSB.flush();           
              }
            }
          } 
          
          // ==========================================
          // LÓGICA DE QUANDO A TECLA É SOLTA
          // ==========================================
          else {
            // Se o botão solto NÃO for um dos botões de função:
            if (keyIndex != BTN_OCTAVE_DOWN && keyIndex != BTN_OCTAVE_UP && keyIndex != BTN_SYSTEM) {
              
              // Verifica a memória para saber exatamente qual número de nota MIDI havia sido enviado por esta tecla.
              if (notasAtivas[keyIndex] > 0) {
                // Desliga exatamente aquela nota registrada.
                noteOff(0, notasAtivas[keyIndex], 0);   
                MidiUSB.flush();
                // Limpa o espaço da memória para esta tecla.
                notasAtivas[keyIndex] = 0; 
              }
            }
          }
        }
      }
      // Atualiza o estado antigo com a leitura mecânica crua atual para a próxima volta do loop.
      lastKeyState[keyIndex] = reading;
    }
    // Antes de ir para a próxima linha da matriz, desliga a linha atual devolvendo ela para 5V (HIGH).
    digitalWrite(rowPins[r], HIGH); 
  }
}

// ==========================================
// FUNÇÕES DE COMUNICAÇÃO MIDI USB
// ==========================================

// Função que constrói o pacote de dados exigido pela especificação MIDI para "Ligar" uma nota (Note On).
void noteOn(byte channel, byte pitch, byte velocity) {
  // 0x09 é o cabeçalho USB para "Note On". 0x90 é o código MIDI para "Note On no canal 0".
  midiEventPacket_t noteOn = {0x09, (byte)(0x90 | channel), pitch, velocity};
  // Envia o pacote para o buffer do USB.
  MidiUSB.sendMIDI(noteOn);
}

// Função que constrói o pacote de dados para "Desligar" uma nota (Note Off).
void noteOff(byte channel, byte pitch, byte velocity) {
  // 0x08 é o cabeçalho USB para "Note Off". 0x80 é o código MIDI para "Note Off no canal 0".
  midiEventPacket_t noteOff = {0x08, (byte)(0x80 | channel), pitch, velocity};
  // Envia o pacote para o buffer do USB.
  MidiUSB.sendMIDI(noteOff);
}