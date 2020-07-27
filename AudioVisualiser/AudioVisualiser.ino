#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>

// GUItool: begin automatically generated code
AudioInputI2S            I2S;           //xy=224,80
AudioAnalyzeFFT1024      fftR;      //xy=507,146
AudioAnalyzePeak         peakL;          //xy=510,287
AudioAnalyzeFFT1024      fftL;      //xy=512,84
AudioAnalyzePeak         peakR;          //xy=511,354
AudioAnalyzeRMS          rmsL;           //xy=512,463
AudioAnalyzeRMS          rmsR;           //xy=523,541
AudioConnection          patchCord1(I2S, 0, fftL, 0);
AudioConnection          patchCord2(I2S, 0, peakL, 0);
AudioConnection          patchCord3(I2S, 0, rmsL, 0);
AudioConnection          patchCord4(I2S, 1, fftR, 0);
AudioConnection          patchCord5(I2S, 1, peakR, 0);
AudioConnection          patchCord6(I2S, 1, rmsR, 0);
// GUItool: end automatically generated code

// declare teensy audio-shield
AudioControlSGTL5000 audioShield;

#include <Adafruit_GFX.h>
#include <LEDMatrix.h> // specific library for my display, this will probably be different for you

// 1000/TARGET_FRAMETIME = the update rate, for 10 ms it's 100 fps
// (it's expected the display will probably update faster than this)
#define TARGET_FRAMETIME 10 // in milliseconds
// size of moving average for mean peak and rms values, also used for auto-scaling 
#define AVERAGE_FOR 400 // frames (at 100 fps, 4 seconds)
// apply a linear scale to peak and rms readings
#define SCALE_PEAKRMS 1
// length of horizontal peak and rms bars
#define HORZ_BAR_LEN 10
// 10 wide, not 12, to leave a blank column spacing before and between FFTs

// background brightness
//#define BG_BRIGHT 0
#define BG_BRIGHT 1

// class to help animate bars from a old value to a new value
// bar length is specified as a number of pixels
// the value shown by the bar is specified 0-65535 (0-1 effective)
// the bar fills up fully to brightness with the tip shaded by the remainder
class Bar {
  public:
    Bar(uint8_t numLeds_, uint8_t brightness_) {
      numLeds = numLeds_;
      oldvalue = 0;
      newvalue = 0;
      brightness = brightness_;
    }
    
    // type conversion for when doing math, probably a better way to do this
    void setValue(double in) {
      setValue(float(in));
    }

    // cast float bar length to the integer for the bar's internal workings
    void setValue(float in) {
      if (in > 65534) {
        setValue(uint16_t(65534));
      }
      else {
        setValue(uint16_t(in));
      }
    }

    // set the value the bar is representing
    // the new value is a combination of new and old value to ensure smoothe animation
    void setValue(uint16_t in) {
      in = in / 4 + 3 * newvalue / 4;
      oldvalue = newvalue;
      newvalue = in;
      change = (newvalue - oldvalue);
    }

    // work out how many pixels should be lit fully (len)
    // and how bright the last tip of the bar should be (remainder)
    // depending on how long it's been since the bar was last updated
    void getBarLength(unsigned long us, uint8_t *len, uint8_t *remainder) {
      float fraction = us / (TARGET_FRAMETIME * 1000); // how far along the animation we are
      float newlen = (change * fraction + oldvalue) * numLeds / 65355;
      *len = floor(newlen);
      *remainder = round((newlen - *len) * (brightness - BG_BRIGHT)) + BG_BRIGHT;
    }

    uint8_t getBrightness() {
      return brightness;
    }

  private:
    uint8_t numLeds;
    uint16_t oldvalue;
    uint16_t newvalue;
    int64_t change;
    uint8_t brightness;
};

// initialuse the Adafruit_GFX enabled display, this will probably be different for you
LEDMatrix gfx(32, 8, SPI1, 31, 30, 29, 28, 20, 0);

// a timer for updating the display regularly
IntervalTimer myTimer;

// function myTimer will call
void myDisplay() { // easiest way around member function pointer
  gfx.displayRow(); // the update function for my particular display, this will probably be different for you
}

// declare horizontal peak and rms bars
Bar peakLbar(HORZ_BAR_LEN, gfx.getBrightnessLevels()/4); // left
Bar peakRbar(HORZ_BAR_LEN, gfx.getBrightnessLevels()/4); // right
Bar rmsLbar(HORZ_BAR_LEN, gfx.getBrightnessLevels());
Bar rmsRbar(HORZ_BAR_LEN, gfx.getBrightnessLevels());
Bar meanpeakbar(HORZ_BAR_LEN, gfx.getBrightnessLevels()/4); // this bar compared to the next bar shows scaling factor
Bar meanpeakbar2(HORZ_BAR_LEN, gfx.getBrightnessLevels()/4);
Bar meanrmsbar(HORZ_BAR_LEN, gfx.getBrightnessLevels());
// note gfx.getBrightnessLevels())
// array of horizontal bars to make iterating through them to update easier
Bar* horizontalBars[] = {&peakLbar, &peakRbar, &rmsLbar, &rmsRbar, &meanpeakbar, &meanpeakbar2, &meanrmsbar};

// number of FFT bars, this should match this length of bucketsplus
#define NUM_FFT_BARS 10
// which fft bins to group (this is a differential)
// do a cumulative sum, then multiply by 1024 / 44100 to get frequencies
// these bins should be around 40, 170, 300, 430, 600, 950, 1600, 2900, 5200, and 8200 Hz
uint8_t bucketsplus[] = {2, 2, 2, 2, 4, 10, 20, 40, 60, 80};
// declare vertical bars for FFT
Bar* fftLbars[NUM_FFT_BARS];
Bar* fftRbars[NUM_FFT_BARS];

// array to store moving average of peak and rms values
uint16_t ui16peakL_log[AVERAGE_FOR];
uint16_t ui16peakR_log[AVERAGE_FOR];
uint16_t ui16rmsL_log[AVERAGE_FOR];
uint16_t ui16rmsR_log[AVERAGE_FOR];
uint16_t averageAt = 0; // position in array

// elapsed time timers since last for animation
elapsedMillis framedelay;
elapsedMicros barAnimation;


// main arduino initialisation
void setup() {
  // initialise the vertical bars
  for (uint8_t i = 0; i < NUM_FFT_BARS; i++) {
    fftLbars[i] = new Bar(8, gfx.getBrightnessLevels()/5*3);
    fftRbars[i] = new Bar(8, gfx.getBrightnessLevels()/5*3);
  }

  // initialise arrays
  memset(ui16peakL_log, 0, AVERAGE_FOR*sizeof(uint16_t));
  memset(ui16peakR_log, 0, AVERAGE_FOR*sizeof(uint16_t));
  memset(ui16rmsL_log, 0, AVERAGE_FOR*sizeof(uint16_t));
  memset(ui16rmsR_log, 0, AVERAGE_FOR*sizeof(uint16_t));

  // ~12 for each FFT and 6 for stereo peak & rms (this is just a guess though)
  AudioMemory(30);

  // enable the audio shield & select line input
  audioShield.enable();
  audioShield.inputSelect(AUDIO_INPUT_LINEIN);

  gfx.begin(25000000); // initialise my GFX display, this will probably be different for you
  myTimer.begin(myDisplay, 400); // set the display to update on an interrupt timer for smoothe animation
}


// main arduino execution loop
void loop() {
  if (framedelay > TARGET_FRAMETIME) { // only try and update the bars if we've waited for the next frame
    if (fftL.available() && fftR.available() && peakL.available() && // only update if all of the information
        peakR.available() && rmsL.available() && rmsR.available()) { // needed for the next update is available
      framedelay = 0; // reset timers
      barAnimation = 0;

      // read and store peak and rms values, log base 10 transforming
      // then scaling that 0 to 65535 for (maybe?) quicker executing bar animation
      uint16_t ui16peakL = log10(9 * peakL.read() + 1) * 65355 * SCALE_PEAKRMS;
      uint16_t ui16peakR = log10(9 * peakR.read() + 1) * 65355 * SCALE_PEAKRMS;
      uint16_t ui16rmsL = log10(9 * rmsL.read() + 1) * 65355 * SCALE_PEAKRMS;
      uint16_t ui16rmsR = log10(9 * rmsR.read() + 1) * 65355 * SCALE_PEAKRMS;

      // store current reading for moving average
      ui16peakL_log[averageAt] = ui16peakL;
      ui16peakR_log[averageAt] = ui16peakR;
      ui16rmsL_log[averageAt] = ui16rmsL;
      ui16rmsR_log[averageAt] = ui16rmsR;

      // calculate the average
      unsigned long meanpeak = 0;
      unsigned long meanrms = 0;
      for (unsigned int i = 0; i < AVERAGE_FOR; i++) {
        meanpeak += ui16peakL_log[i] + ui16peakR_log[i];
        meanrms += ui16rmsL_log[i] + ui16rmsR_log[i];
      }
      meanpeak = meanpeak / (2 * AVERAGE_FOR);
      meanrms = meanrms / (2 * AVERAGE_FOR);

      // calculate dynamic scaling coefficient
      // (this could probably done 100% integer for a speed-up)
      float scalepeak = meanpeak / 65355.0f;
      if (scalepeak > 0.9) {
        scalepeak = 0.9 / scalepeak;
      }
      else if (scalepeak > 0.7) {
        scalepeak = 1;
      }
      else if (scalepeak > 0.005 && scalepeak <= 0.7) {
        scalepeak = 0.7 / scalepeak;
      }

      // apply scaling to and update horizontal bars
      peakLbar.setValue(ui16peakL * scalepeak);
      peakRbar.setValue(ui16peakR * scalepeak);
      rmsLbar.setValue(ui16rmsL * scalepeak);
      rmsRbar.setValue(ui16rmsR * scalepeak);

      meanpeakbar2.setValue(meanpeak * scalepeak);
      meanpeakbar.setValue(uint16_t(meanpeak));
      meanrmsbar.setValue(meanrms * scalepeak);

      // read and scale FFT readings
      uint16_t bat = 0, batnew;
      for (uint8_t i = 0; i < NUM_FFT_BARS; i++) {
        batnew = bat + bucketsplus[i];
        if (scalepeak <= 1) {
          fftLbars[i]->setValue(log10((9 * fftL.read(bat, batnew - 1) + 1) - (i*0.002)) * 65354.0 * scalepeak);
          fftRbars[i]->setValue(log10((9 * fftR.read(bat, batnew - 1) + 1) - (i*0.002)) * 65354.0 * scalepeak);
        }
        else {
          fftLbars[i]->setValue(log10((9 * fftL.read(bat, batnew - 1) * scalepeak + 1) - (i*0.002*scalepeak)) * 65354.0 * 2);
          fftRbars[i]->setValue(log10((9 * fftR.read(bat, batnew - 1) * scalepeak + 1) - (i*0.002*scalepeak)) * 65354.0 * 2);          
        }
        bat = batnew;
      }

      // cycle around index for the moving average
      averageAt++;
      if (averageAt == AVERAGE_FOR) {
        averageAt = 0;
      }
    }
  }

  // only draw next frame when current frame has been shown (i.e. the interrupt has triggered)
  // my display has a function for this, you may need to write your own or remove this
  if (gfx.wasLastFrameDrawn()) {

    // draw frame
    // start by resetting by filling with background colour
    gfx.fillScreen(BG_BRIGHT);

    // variables used in drawing bars
    uint8_t len, remainder, extra = 0;

    // draw the horizontal bars
    for (uint8_t i = 0; i < 7; i++ ) {
      // get the number of solid pixels and the intensity of the final pixel of the current bar
      horizontalBars[i]->getBarLength(long(barAnimation), &len, &remainder); 
      if (len != 0) {
        gfx.drawLine(0, i + extra, len - 1, i + extra, horizontalBars[i]->getBrightness()); // len - 1 because 0 offset counting pixels
        if (len < HORZ_BAR_LEN) { // I'm not 100% sure this is guaranteed to be true, so check
          gfx.drawPixel(len, i + extra, remainder);
        }
      }
      if (i == 3) { // this shuffles the bottom three bars down by one pixel
        extra = 1;
      }
    }

    // draw the left channel FFT, this is largely copy paste from above
    for (uint8_t i = 0; i < NUM_FFT_BARS; i++ ) {
      fftLbars[i]->getBarLength(long(barAnimation), &len, &remainder);
      if (len != 0) {
        gfx.drawLine(11 + i, 7 - (len - 1), 11 + i, 7, fftLbars[i]->getBrightness()); // len - 1 because 0 offset counting pixels
        if (len < 8) {
          gfx.drawPixel(11 + i, 7 - len, remainder);
        }
      }
    }

    // draw the right channel FFT, this is copy paste from above, just L changed to R
    for (uint8_t i = 0; i < NUM_FFT_BARS; i++ ) {
      fftRbars[i]->getBarLength(long(barAnimation), &len, &remainder);
      if (len != 0) {
        gfx.drawLine(22 + i, 7 - (len - 1), 22 + i, 7, fftRbars[i]->getBrightness()); // len - 1 because 0 offset counting pixels
        if (len < 8) {
          gfx.drawPixel(22 + i, 7 - len, remainder);
        }
      }
    }

    // make the display buffer we've been just writing to the one that will be displayed
    // this may be unique to the display I'm using, so you may not need this
    gfx.flip();
  }
}
