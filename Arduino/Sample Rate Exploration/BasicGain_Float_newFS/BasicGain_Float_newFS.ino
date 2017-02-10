/*
   BasicGain

   Created: Chip Audette, Nov 2016
   Purpose: Process audio by applying gain.
            Demonstrates audio processing using floating point data type.

   Uses Teensy Audio Adapter.
   Assumes microphones (or whatever) are attached to the LINE IN (stereo)
   Use potentiometer mounted to Audio Board to control the amount of gain.

   MIT License.  use at your own risk.
*/

#define CUSTOM_SAMPLE_RATE 24000     //See local AudioStream.h.  Only a limitted number supported
#define CUSTOM_BLOCK_SAMPLES 32     //See local AudioStream.h.  Only a limitted number supported

//include my custom AudioStream.h...this prevents the default one from being used
#include "AudioStream_Mod.h"

//These are the includes from the Teensy Audio Library
#include <Audio.h>      //Teensy Audio Library
#include <Wire.h>
#include <SPI.h>
//#include <SD.h>
//#include <SerialFlash.h>

#include <OpenAudio_ArduinoLibrary.h> //for AudioConvert_I16toF32, AudioConvert_F32toI16, and AudioEffectGain_F32
#include "SampleRateFuncs.h"

//expose the sample rate
float actual_fs_Hz = AUDIO_SAMPLE_RATE;  //right now, is appoximate.  will get properly defined when we issue the command to set the sample rate

//create audio library objects for handling the audio
AudioControlSGTL5000    sgtl5000_1;    //controller for the Teensy Audio Board
AudioInputI2S           i2s_in;        //Digital audio *from* the Teensy Audio Board ADC.  Sends Int16.  Stereo.
AudioOutputI2S          i2s_out;       //Digital audio *to* the Teensy Audio Board DAC.  Expects Int16.  Stereo
AudioConvert_I16toF32   int2Float1, int2Float2;    //Converts Int16 to Float.  See class in AudioStream_F32.h
AudioEffectGain_F32     gain1, gain2;  //Applies digital gain to audio data.  Expected Float data.  
AudioConvert_F32toI16   float2Int1, float2Int2;    //Converts Float to Int16.  See class in AudioStream_F32.h
AudioSynthWaveformSine   testSignal;          //xy=107,185

//Make all of the audio connections
#if 1
  AudioConnection         patchCord1(testSignal,0,i2s_out,0);
  AudioConnection         patchCord2(testSignal,0,i2s_out,1);
#else
  #if 0
    AudioConnection         patchCord1(i2s_in, 0, int2Float1, 0);    //connect the Left input to the Left Int->Float converter
    AudioConnection         patchCord2(i2s_in, 1, int2Float2, 0);    //connect the Right input to the Right Int->Float converter
  #else
    AudioConnection         patchCord1(testSignal, 0, int2Float1, 0);    //connect the Left input to the Left Int->Float converter
    AudioConnection         patchCord2(testSignal, 0, int2Float2, 0);    //connect the Right input to the Right Int->Float converter
  #endif
  AudioConnection_F32     patchCord10(int2Float1, 0, gain1, 0);    //Left.  makes Float connections between objects
  AudioConnection_F32     patchCord11(int2Float2, 0, gain2, 0);    //Right.  makes Float connections between objects
  AudioConnection_F32     patchCord12(gain1, 0, float2Int1, 0);    //Left.  makes Float connections between objects
  AudioConnection_F32     patchCord13(gain2, 0, float2Int2, 0);    //Right.  makes Float connections between objects
  AudioConnection         patchCord20(float2Int1, 0, i2s_out, 0);  //connect the Left float processor to the Left output
  AudioConnection         patchCord21(float2Int2, 0, i2s_out, 1);  //connect the Right float processor to the Right output
#endif

// which input on the audio shield will be used?
const int myInput = AUDIO_INPUT_LINEIN;
//const int myInput = AUDIO_INPUT_MIC;

//I have a potentiometer on the Teensy Audio Board
#define POT_PIN A1  //potentiometer is tied to this pin

// define the setup() function, the function that is called once when the device is booting
void setup() {
  Serial.begin(115200);   //open the USB serial link to enable debugging messages
  delay(1000);             //give the computer's USB serial system a moment to catch up.
  Serial.println("GenericHearingAid: setup()...");
  Serial.print("Global: F_CPU: "); Serial.println(F_CPU); 
  Serial.print("Global: F_PLL: "); Serial.println(F_PLL);
  Serial.print("Global: AUDIO_SAMPLE_RATE: "); Serial.println(AUDIO_SAMPLE_RATE);
  Serial.print("Global: AUDIO_BLOCK_SAMPLES: "); Serial.println(AUDIO_BLOCK_SAMPLES);

  // Audio connections require memory
  AudioMemory_F32(10);  //allocate Float32 audio data blocks
  AudioMemory(10);      //allocate Int16 audio data blocks

  //change the sample rate
  actual_fs_Hz = setI2SFreq((int)AUDIO_SAMPLE_RATE); //set the sample rate for the Audio Card (the rest of the library doesn't know, though)
  Serial.print("Changed sample rate.  Actual value (Hz) = "); Serial.println(actual_fs_Hz);
 
  // Enable the audio shield, select input, and enable output
  sgtl5000_1.enable();                   //start the audio board
  sgtl5000_1.inputSelect(myInput);       //choose line-in or mic-in
  sgtl5000_1.volume(0.8);                //volume can be 0.0 to 1.0.  0.5 seems to be the usual default.
  sgtl5000_1.lineInLevel(10,10);         //level can be 0 to 15.  5 is the Teensy Audio Library's default
  sgtl5000_1.adcHighPassFilterDisable(); //reduces noise.  https://forum.pjrc.com/threads/27215-24-bit-audio-boards?p=78831&viewfull=1#post78831

  //setup sine wave as test signal
  testSignal.amplitude(0.05);
  testSignal.frequency(500.0f);
  
  // setup any other other features
  pinMode(POT_PIN, INPUT); //set the potentiometer's input pin as an INPUT

} //end setup()


// define the loop() function, the function that is repeated over and over for the life of the device
unsigned long updatePeriod_millis = 100; //how many milliseconds between updating gain reading?
unsigned long lastUpdate_millis = 0;
unsigned long curTime_millis = 0;
int prev_gain_dB = 0;
void loop() {
  //choose to sleep ("wait for interrupt") instead of spinning our wheels doing nothing but consuming power
  asm(" WFI");  //ARM-specific.  Will wake on next interrupt.  The audio library issues tons of interrupts, so we wake up often.
  
  //update the memory and CPU usage...if enough time has passed
  printMemoryAndCPU(millis());
  
  //has enough time passed to try updating the GUI?
  curTime_millis = millis(); //what time is it right now
  if (curTime_millis < lastUpdate_millis) lastUpdate_millis = 0; //handle wrap-around of the clock
  if ((curTime_millis - lastUpdate_millis) > updatePeriod_millis) { //is it time to update the user interface?
    
    //read potentiometer
    float val = float(analogRead(POT_PIN)) / 1024.0; //0.0 to 1.0
    val = 0.1*(float)((int)(10.0*val + 0.5));  //quantize so that it doesn't chatter
        
    //compute desired digital gain
    const float min_gain_dB = -20.0, max_gain_dB = 40.0; //set desired gain range
    float gain_dB = min_gain_dB + (max_gain_dB - min_gain_dB)*val; //computed desired gain value in dB

    //if the gain is different than before, set the new gain value
    if (abs(gain_dB - prev_gain_dB) > 1.0) { //is it different than before
      gain1.setGain_dB(gain_dB);  //set the gain of the Left-channel gain processor
      gain2.setGain_dB(gain_dB);  //set the gain of the Right-channel gain processor
      Serial.print("Digital Gain dB = "); Serial.println(gain_dB); //print text to Serial port for debugging
      prev_gain_dB = gain_dB;  //we will use this value the next time around
    }
 
    lastUpdate_millis = curTime_millis; //we will use this value the next time around.
  } // end if

} //end loop();


void printMemoryAndCPU(unsigned long curTime_millis) {
  static unsigned long updatePeriod_millis = 2000; //how many milliseconds between updating gain reading?
  static unsigned long lastUpdate_millis = 0;

  //has enough time passed to update everything?
  if (curTime_millis < lastUpdate_millis) lastUpdate_millis = 0; //handle wrap-around of the clock
  if ((curTime_millis - lastUpdate_millis) > updatePeriod_millis) { //is it time to update the user interface?
    Serial.print("CPU: ");
    Serial.print(AudioProcessorUsage_wFS(actual_fs_Hz));
    Serial.print(", Max ");
    Serial.print(AudioProcessorUsageMax_wFS(actual_fs_Hz));
//    Serial.print(AudioProcessorUsage());
//    Serial.print(", Max ");
//    Serial.print(AudioProcessorUsageMax());
    Serial.print(",   ");
    Serial.print("Int16 Mem Used: ");
    Serial.print(AudioMemoryUsage());
    Serial.print(", Max ");
    Serial.print(AudioMemoryUsageMax());
    Serial.print(",   ");
    Serial.print("Float Mem Used: ");
    Serial.print(AudioMemoryUsage_F32());
    Serial.print(", Max ");
    Serial.print(AudioMemoryUsageMax_F32());
    Serial.println();
    lastUpdate_millis = curTime_millis; //we will use this value the next time around.
  }
}

