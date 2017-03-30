#include <Tympan_Library.h>
#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>

#include "AudioEffectExpander.h"
//include "AudioMultiblockAverage.h"

//use the expansion or not?  see setupExpanders() for more  settings
#define USE_EXPANSION 1   //set to 1 to use expansion, set to 0 to defeat it

const float sample_rate_Hz = 24000.f ; //24000 or 44117.64706f (or other frequencies in the table in AudioOutputI2S_F32
const int audio_block_samples = 32;  //do not make bigger than AUDIO_BLOCK_SAMPLES from AudioStream.h (which is 128)
AudioSettings_F32   audio_settings(sample_rate_Hz, audio_block_samples);

const float input_gain_dB = 15.0f; //gain on the microphone
const int N_CHAN = 4;  //number of frequency bands (channels)

// GUItool: begin automatically generated code
AudioInputI2S_F32        audioInI2S1(audio_settings);    //xy=126,110
AudioFilterIIR_F32       iir_hp;
AudioFilterFIR_F32       fir[N_CHAN];           //xy=223,216
AudioEffectExpander_F32  expander[N_CHAN];
AudioMixer4_F32             mixer4_1;       //xy=587,275
AudioEffectCompressor_F32   limiter1;     //xy=717,184
AudioOutputI2S_F32          audioOutI2S1(audio_settings);   //xy=860,104
AudioControlTLV320AIC3206   audioHardware; //xy=161,42
AudioConfigFIRFilterBank_F32 configFIRFilterBank1; //xy=349,45

AudioConnection_F32         patchCord0(audioInI2S1, 0, iir_hp, 0);
AudioConnection_F32         patchCord1(iir_hp, 0, fir[0], 0);
AudioConnection_F32         patchCord2(iir_hp, 0, fir[1], 0);
AudioConnection_F32         patchCord3(iir_hp, 0, fir[2], 0);
AudioConnection_F32         patchCord4(iir_hp, 0, fir[3], 0);
AudioConnection_F32         patchCord11(fir[0], 0, expander[0], 0);
AudioConnection_F32         patchCord12(fir[1], 0, expander[1], 0);
AudioConnection_F32         patchCord13(fir[2], 0, expander[2], 0);
AudioConnection_F32         patchCord14(fir[3], 0, expander[3], 0);
AudioConnection_F32         patchCord21(expander[0], 0, mixer4_1, 0);
AudioConnection_F32         patchCord22(expander[1], 0, mixer4_1, 1);
AudioConnection_F32         patchCord23(expander[2], 0, mixer4_1, 2);
AudioConnection_F32         patchCord24(expander[3], 0, mixer4_1, 3);

AudioConnection_F32         patchCord30(mixer4_1, limiter1);
AudioConnection_F32         patchCord31(limiter1, 0, audioOutI2S1, 0);
AudioConnection_F32         patchCord32(limiter1, 0, audioOutI2S1, 1);


// GUItool: end automatically generated code

#define POT_PIN A1

//setup the tympan
void setupAudioHardware(void) {
    // Setup the TLV320
  audioHardware.enable(); // activate AIC

  // Choose the desired input
  //tlv320aic3206_1.inputSelect(TYMPAN_INPUT_ON_BOARD_MIC); // use the on board microphones // default
  audioHardware.inputSelect(TYMPAN_INPUT_JACK_AS_MIC); // use the microphone jack - defaults to mic bias 2.5V
  //  tlv320aic3206_1.inputSelect(TYMPAN_INPUT_JACK_AS_LINEIN); // use the microphone jack - defaults to mic bias OFF
  //  tlv320aic3206_1.inputSelect(TYMPAN_INPUT_LINE_IN); // use the line in pads on the TYMPAN board - defaults to mic bias OFF

  //Adjust the MIC bias, if using TYMPAN_INPUT_JACK_AS_MIC
  //  tlv320aic3206_1.setMicBias(TYMPAN_MIC_BIAS_OFF); // Turn mic bias off
  audioHardware.setMicBias(TYMPAN_MIC_BIAS_2_5); // set mic bias to 2.5 // default

  // set initial VOLUMES
  audioHardware.volume_dB(0);  // -63.6 to +24 dB in 0.5dB steps.  uses float
  audioHardware.setInputGain_dB(input_gain_dB); // set MICPGA volume, 0-47.5dB in 0.5dB setps

  //setup the potentiometer as an input
  pinMode(POT_PIN,INPUT);
}

#define N_FIR 96
#define FS_HZ audio_settings.sample_rate_Hz
float fir_coeff[N_CHAN][N_FIR];

//setup algorithm parameters
void setupAlgorithms(void) {
  
  //setup the leading high-pass filter
  //N_iir=2; cutoff_Hz = 125; fs_Hz = 24000; ftype = 'highpass'; b, a = signal.butter(N_iir, cutoff_Hz/(fs_Hz/2.0),ftype)
  float b[] = { 0.977125627265454,  -1.954251254530908,   0.977125627265454 }; //matlab-style filter definition
  float a[] = { 1.f        , -1.953727949140776,   0.954774559921040 }; //matlab-style filter definition
  iir_hp.setFilterCoeff_Matlab(b,a); //define IIR filter using matlab-style arrays
  
  //setup the filter for each channel
  float corner_freq[] = { 750.f, 1500.f, 3000.f };  //needs to be (N_CHAN - 1)
  configFIRFilterBank1.createFilterCoeff(N_CHAN, N_FIR, FS_HZ, &corner_freq[0], &fir_coeff[0][0]);
  for (int i=0; i< N_CHAN; i++) fir[i].begin(fir_coeff[i], N_FIR, audio_settings.audio_block_samples);

  //setup the expanders
  setupExpanders(corner_freq);
 
  //setup the final compressor as a limitter
  float attack_msec = 5.0, release_msec = 200.f;
  limiter1.setAttack_sec(attack_msec / 1000.f, FS_HZ);
  limiter1.setRelease_sec(release_msec / 1000.f, FS_HZ);
  limiter1.setPreGain_dB(0.f);
  limiter1.setThresh_dBFS(-10.f);
  limiter1.setCompressionRatio(5.f);     
}

void setupExpanders(float *corner_freq_Hz) {
  //choose the attack and release for the expanders
  float attack_ms = 5.f, release_ms = 300.f;  
  
  //choose where to put the thresholds.  base
  float thresh_dBFS[N_CHAN]; //declare, set the values later 
  const float baseline_density_dBFS_for_1kHzBW = -95.0f + 10.0f;  //set 10dB above the noise floor
  const float baseline_density_per_Hz = powf(10.0,0.1*baseline_density_dBFS_for_1kHzBW) / 1000.f;

  //choose the other parameters
  float expand_ratio[N_CHAN]; //declare, set the values later
  #if USE_EXPANSION
    const float default_expand_ratio = 1.75f; //a value of 1.0 basically defeats the expander
  #else
    //defeat the expansion
    const float default_expand_ratio = 1.f;
  #endif
  float linear_gain_dB = 0.f; //applied after comparison to threshold
  
  for (int i=0;i<N_CHAN; i++) {
    expander[i].setAudioSettings(audio_settings); //give it the basic audio settings (sample rate, block size)
    if (i==0) {  
      //set the bottom band to be different from the rest (less or no expansion)
      thresh_dBFS[0] = -85.f+10.f; expand_ratio[0] = (default_expand_ratio-1.f)/2.f + 1.f;  //less expansion on the lowest
    } else {
      //set the rest of the bands
      float BW_Hz;
      if (i < (N_CHAN-1)) { 
        BW_Hz = corner_freq_Hz[i]-corner_freq_Hz[i-1]; 
      } else { 
        BW_Hz = audio_settings.sample_rate_Hz / 2.0 - corner_freq_Hz[N_CHAN-1];
      };
      //the rest of the bands...scale the threshold for expansion by the bandwidth (assumes the noise is basically white)
      thresh_dBFS[i] = 10.f*log10f(baseline_density_per_Hz*BW_Hz);
      expand_ratio[i] = default_expand_ratio; //same expansioni ratio for all bands after the first
    }
    Serial.print("setupExpanders: Channel "); Serial.print(i); 
    Serial.print(": thresh dBFS = "); Serial.print(thresh_dBFS[i]);
    Serial.print(": expansion ratio = "); Serial.print(expand_ratio[i]);
    Serial.println();
    expander[i].setParams(attack_ms, release_ms, thresh_dBFS[i], expand_ratio[i], linear_gain_dB);
  }

}

//The setup function is called once when the system starts up
void setup(void) {
  //Start the USB serial link (to enable debugging)
  Serial.begin(115200); delay(500);
  Serial.println("Setup starting...");
  
  //Allocate dynamically shuffled memory for the audio subsystem
  AudioMemory(10);  AudioMemory_F32_wSettings(20,audio_settings);
  
  //Put your own setup code here
  setupAudioHardware();
  setupAlgorithms();
  
  //End of setup
  Serial.println("Setup complete.");
};


//After setup(), the loop function loops forever.
//Note that the audio modules are called in the background.
//They do not need to be serviced by the loop() function.
void loop(void) {
   //choose to sleep ("wait for interrupt") instead of spinning our wheels doing nothing but consuming power
  asm(" WFI");  //ARM-specific.  Will wake on next interrupt.  The audio library issues tons of interrupts, so we wake up often.

  //service the potentiometer...if enough time has passed
  servicePotentiometer(millis());

  //update the memory and CPU usage...if enough time has passed
  printMemoryAndCPU(millis());

  //print info about the signal processing
  printUpdatedLevels(millis());
}



//servicePotentiometer: listens to the blue potentiometer and sends the new pot value
//  to the audio processing algorithm as a control parameter
void servicePotentiometer(unsigned long curTime_millis) {
  static unsigned long updatePeriod_millis = 100; //how many milliseconds between updating the potentiometer reading?
  static unsigned long lastUpdate_millis = 0;
  static float prev_val = 0;

  //has enough time passed to update everything?
  if (curTime_millis < lastUpdate_millis) lastUpdate_millis = 0; //handle wrap-around of the clock
  if ((curTime_millis - lastUpdate_millis) > updatePeriod_millis) { //is it time to update the user interface?

    //read potentiometer
    float val = float(analogRead(POT_PIN)) / 1024.0; //0.0 to 1.0
    val = 1.0/9.0 * (float)((int)(9.0* val + 0.5)); //quantize so that it doesn't chatter...0 to 1.0
    
    //send the potentiometer value to your algorithm as a control parameter
    //float scaled_val = val / 3.0; scaled_val = scaled_val * scaled_val;
    if (abs(val - prev_val) > 0.05) { //is it different than befor?
      prev_val = val;  //save the value for comparison for the next time around
      val = 1.0 - val; //reverse direction of potentiometer (error with Tympan PCB)
      
      float total_gain_dB = val*45.0+15.0;  //span 0 to 45
      float linear_gain_dB = total_gain_dB - input_gain_dB;
      Serial.print("Total Gain = "); Serial.print(total_gain_dB); Serial.print(" dB, ");
      Serial.print("Mic Gain = "); Serial.print(input_gain_dB); Serial.print(" dB, ");
      Serial.print("Exp Linear Gain = "); Serial.print(linear_gain_dB); Serial.println();
      for (int i=0; i<N_CHAN; i++) expander[i].setGain_dB(linear_gain_dB); //change gain on all bands
     
    }
    lastUpdate_millis = curTime_millis;
  } // end if
} //end servicePotentiometer();

void printMemoryAndCPU(unsigned long curTime_millis) {
  static unsigned long updatePeriod_millis = 3000; //how many milliseconds between updating gain reading?
  static unsigned long lastUpdate_millis = 0;

  //has enough time passed to update everything?
  if (curTime_millis < lastUpdate_millis) lastUpdate_millis = 0; //handle wrap-around of the clock
  if ((curTime_millis - lastUpdate_millis) > updatePeriod_millis) { //is it time to update the user interface?
    Serial.print("CPU Cur/Peak: ");
    Serial.print(audio_settings.processorUsage());
    //Serial.print(AudioProcessorUsage());
    Serial.print("%/");
    Serial.print(audio_settings.processorUsageMax());
    //Serial.print(AudioProcessorUsageMax());
    Serial.print("%,   ");
    Serial.print("Dyn MEM Int16 Cur/Peak: ");
    Serial.print(AudioMemoryUsage());
    Serial.print("/");
    Serial.print(AudioMemoryUsageMax());
    Serial.print(",   ");
    Serial.print("Dyn MEM Float32 Cur/Peak: ");
    Serial.print(AudioMemoryUsage_F32());
    Serial.print("/");
    Serial.print(AudioMemoryUsageMax_F32());
    Serial.println();

    lastUpdate_millis = curTime_millis; //we will use this value the next time around.
  }
}

void printUpdatedLevels(unsigned long curTime_millis) {
  static unsigned long calc_updatePeriod_millis = 100; //how often to perform the averaging
  static unsigned long calc_lastUpdate_millis = 0;
  static unsigned long display_updatePeriod_millis = 3000; //how often to print the levels to the screen
  static unsigned long display_lastUpdate_millis = 0;
  static float ave_dB[N_CHAN];
  float update_coeff = 0.1;

  //is it time to update the calculations
  if (curTime_millis < calc_lastUpdate_millis) calc_lastUpdate_millis = 0; //handle wrap-around of the clock
  if ((curTime_millis - calc_lastUpdate_millis) > calc_updatePeriod_millis) { //is it time to update the user interface?
    for (int i=0; i<N_CHAN; i++) { //loop over each band
      ave_dB[i] = (1.0-update_coeff)*ave_dB[i] + update_coeff*expander[i].getCurrentLevel_dB(); //running average
    }
    calc_lastUpdate_millis = curTime_millis; //we will use this value the next time around.
  }
  
  //is it time to print to the screen
  if (curTime_millis < display_lastUpdate_millis) display_lastUpdate_millis = 0; //handle wrap-around of the clock
  if ((curTime_millis - display_lastUpdate_millis) > display_updatePeriod_millis) { //is it time to update the user interface?   
    Serial.print("Signal Level Per-Band (dBFS) = ");
    for (int i=0; i<N_CHAN; i++) { Serial.print(ave_dB[i]);  Serial.print(", ");  }
    Serial.println();
    
    display_lastUpdate_millis = curTime_millis; //we will use this value the next time around.
  }
}



