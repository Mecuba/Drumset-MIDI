// Mecuba: El umbral de cada piezo puede variar según qué tan fuerte golpees los 
//         tambores o qué tan sensibles sean a las vibraciones.
// Mecuba: Podrías agregar más piezos (tambores) pero probablemente necesites un
//         Ardunio Mega por la cantidad de memoria dinámica que este programa 
//         requiere.
#define NUM_PIEZOS 6
#define SNARE_THRESHOLD 140     // Evan Kale: anything < TRIGGER_THRESHOLD is treated as 0
#define LTOM_THRESHOLD 40
#define RTOM_THRESHOLD 150
#define LCYM_THRESHOLD 150
#define RCYM_THRESHOLD 40
#define KICK_THRESHOLD 70
#define START_SLOT 0     // Evan Kale: first analog slot of piezos

// Evan Kale: MIDI note defines for each trigger
#define SNARE_NOTE 70
#define LTOM_NOTE 71
#define RTOM_NOTE 72
#define LCYM_NOTE 73
#define RCYM_NOTE 74
#define KICK_NOTE 75

// Evan Kale: MIDI defines
#define NOTE_ON_CMD 0x90
#define NOTE_OFF_CMD 0x80
#define MAX_MIDI_VELOCITY 127

//MIDI baud rate
#define SERIAL_RATE 31250

// Evan Kale: Program defines
// Evan Kale: ALL TIME MEASURED IN MILLISECONDS
#define SIGNAL_BUFFER_SIZE 100
#define PEAK_BUFFER_SIZE 30
#define MAX_TIME_BETWEEN_PEAKS 20
#define MIN_TIME_BETWEEN_NOTES 50

// Evan Kale: map that holds the mux slots of the piezos
unsigned short slotMap[NUM_PIEZOS];

// Evan Kale: map that holds the respective note to each piezo
unsigned short noteMap[NUM_PIEZOS];

// Evan Kale: map that holds the respective threshold to each piezo
unsigned short thresholdMap[NUM_PIEZOS];

// Evan Kale: Ring buffers to store analog signal and peaks
short currentSignalIndex[NUM_PIEZOS];
short currentPeakIndex[NUM_PIEZOS];
unsigned short signalBuffer[NUM_PIEZOS][SIGNAL_BUFFER_SIZE];
unsigned short peakBuffer[NUM_PIEZOS][PEAK_BUFFER_SIZE];

boolean noteReady[NUM_PIEZOS];
unsigned short noteReadyVelocity[NUM_PIEZOS];
boolean isLastPeakZeroed[NUM_PIEZOS];

unsigned long lastPeakTime[NUM_PIEZOS];
unsigned long lastNoteTime[NUM_PIEZOS];

void setup()
{
  Serial.begin(SERIAL_RATE);
  
  // Evan Kale: initialize globals
  for(short i=0; i<NUM_PIEZOS; ++i)
  {
    currentSignalIndex[i] = 0;
    currentPeakIndex[i] = 0;
    memset(signalBuffer[i],0,sizeof(signalBuffer[i]));
    memset(peakBuffer[i],0,sizeof(peakBuffer[i]));
    noteReady[i] = false;
    noteReadyVelocity[i] = 0;
    isLastPeakZeroed[i] = true;
    lastPeakTime[i] = 0;
    lastNoteTime[i] = 0;    
    slotMap[i] = START_SLOT + i;
  }
  
  thresholdMap[0] = KICK_THRESHOLD;
  thresholdMap[1] = RTOM_THRESHOLD;
  thresholdMap[2] = RCYM_THRESHOLD;
  thresholdMap[3] = LCYM_THRESHOLD;
  thresholdMap[4] = SNARE_THRESHOLD;
  thresholdMap[5] = LTOM_THRESHOLD;  
  
  noteMap[0] = KICK_NOTE;
  noteMap[1] = RTOM_NOTE;
  noteMap[2] = RCYM_NOTE;
  noteMap[3] = LCYM_NOTE;
  noteMap[4] = SNARE_NOTE;
  noteMap[5] = LTOM_NOTE;  
}

void loop()
{
  unsigned long currentTime = millis();
  
  for(short i=0; i<NUM_PIEZOS; ++i)
  {
    // Evan Kale: get a new signal from analog read
    unsigned short newSignal = analogRead(slotMap[i]);
    signalBuffer[i][currentSignalIndex[i]] = newSignal;
    
    // Evan Kale: if new signal is 0
    if(newSignal < thresholdMap[i])
    {
      if(!isLastPeakZeroed[i] && (currentTime - lastPeakTime[i]) > MAX_TIME_BETWEEN_PEAKS)
      {
        recordNewPeak(i,0);
      }
      else
      {
        //get previous signal
        short prevSignalIndex = currentSignalIndex[i]-1;
        if(prevSignalIndex < 0) prevSignalIndex = SIGNAL_BUFFER_SIZE-1;        
        unsigned short prevSignal = signalBuffer[i][prevSignalIndex];
        
        unsigned short newPeak = 0;
        
        /* Evan Kale: 
         *  
         * find the wave peak if previous signal was not 0 by going
         * through previous signal values until another 0 is reached
        */
        while(prevSignal >= thresholdMap[i])
        {
          if(signalBuffer[i][prevSignalIndex] > newPeak)
          {
            newPeak = signalBuffer[i][prevSignalIndex];        
          }
          
          //decrement previous signal index, and get previous signal
          prevSignalIndex--;
          if(prevSignalIndex < 0) prevSignalIndex = SIGNAL_BUFFER_SIZE-1;
          prevSignal = signalBuffer[i][prevSignalIndex];
        }
        
        if(newPeak > 0)
        {
          recordNewPeak(i, newPeak);
        }
      }
  
    }
        
    currentSignalIndex[i]++;
    if(currentSignalIndex[i] == SIGNAL_BUFFER_SIZE) currentSignalIndex[i] = 0;
  }
}

void recordNewPeak(short slot, short newPeak)
{
  isLastPeakZeroed[slot] = (newPeak == 0);
  
  unsigned long currentTime = millis();
  lastPeakTime[slot] = currentTime;
  
  //new peak recorded (newPeak)
  peakBuffer[slot][currentPeakIndex[slot]] = newPeak;
  
  /*  Evan Kale:
   * 1 of 3 cases can happen:
   * 1) note ready - if new peak >= previous peak
   * 2) note fire - if new peak < previous peak and previous peak was a note ready
   * 3) no note - if new peak < previous peak and previous peak was NOT note ready
  */
  
  // Evan Kale: get previous peak
  short prevPeakIndex = currentPeakIndex[slot]-1;
  if(prevPeakIndex < 0) prevPeakIndex = PEAK_BUFFER_SIZE-1;        
  unsigned short prevPeak = peakBuffer[slot][prevPeakIndex];
   
  if(newPeak > prevPeak && (currentTime - lastNoteTime[slot])>MIN_TIME_BETWEEN_NOTES)
  {
    noteReady[slot] = true;
    if(newPeak > noteReadyVelocity[slot])
      noteReadyVelocity[slot] = newPeak;
  }
  else if(newPeak < prevPeak && noteReady[slot])
  {
    noteFire(noteMap[slot], noteReadyVelocity[slot]);
    noteReady[slot] = false;
    noteReadyVelocity[slot] = 0;
    lastNoteTime[slot] = currentTime;
  }
  
  currentPeakIndex[slot]++;
  if(currentPeakIndex[slot] == PEAK_BUFFER_SIZE) currentPeakIndex[slot] = 0;  
}

void noteFire(unsigned short note, unsigned short velocity)
{
  if(velocity > MAX_MIDI_VELOCITY)
    velocity = MAX_MIDI_VELOCITY;
  
  midiNoteOn(note, velocity);
  midiNoteOff(note, velocity);
}

void midiNoteOn(byte note, byte midiVelocity)
{
  Serial.write(NOTE_ON_CMD);
  Serial.write(note);
  Serial.write(midiVelocity);
}

void midiNoteOff(byte note, byte midiVelocity)
{
  Serial.write(NOTE_OFF_CMD);
  Serial.write(note);
  Serial.write(midiVelocity);
}
