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

#include <Adafruit_GFX.h>
#include <LEDMatrix.h>

#define TARGET_FRAMETIME 10 // milliseconds 100 fps
#define AVERAGE_FOR 400 // frames (at 100 fps, 4 seconds)
//#define SCALE_PEAKRMS 1
#define SCALE_PEAKRMS 1
// 10 wide, not 12, to leave a blank column spacing before and between FFTs
#define HORZ_BAR_LEN 10

//#define BG_BRIGHT 0
#define BG_BRIGHT 1

//#define MAX_BRIGHTNESS_LEVELS 255

class Bar {
  public:
    Bar(uint8_t numLeds_, uint8_t brightness_) {
      numLeds = numLeds_;
      oldvalue = 0;
      newvalue = 0;
      brightness = brightness_;
    }
    
    void setValue(double in) {
      setValue(float(in));
    }

    void setValue(float in) {
      if (in > 65534) {
        setValue(uint16_t(65534));
      }
      else {
        setValue(uint16_t(in));
      }
    }

    void setValue(uint16_t in) {
//      if (in < 8192) {
//        in = newvalue / 2;
//      }
//      in = (9 * (in / 10) + newvalue / 10);
      in = in / 4 + 3 * newvalue / 4;
      oldvalue = newvalue;
      newvalue = in;
      change = (newvalue - oldvalue);
    }

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

LEDMatrix gfx(32, 8, SPI1, 31, 30, 29, 28, 20, 0);

IntervalTimer myTimer;

void myDisplay() { // easiest way around member function pointer
//  long then = micros();
//  gfx.display();
  gfx.displayRow();
//  Serial.println((micros() - then));
}

AudioControlSGTL5000 audioShield;

Bar peakLbar(HORZ_BAR_LEN, gfx.getBrightnessLevels()/4);
Bar peakRbar(HORZ_BAR_LEN, gfx.getBrightnessLevels()/4);
Bar rmsLbar(HORZ_BAR_LEN, gfx.getBrightnessLevels());
Bar rmsRbar(HORZ_BAR_LEN, gfx.getBrightnessLevels());
Bar meanpeakbar(HORZ_BAR_LEN, gfx.getBrightnessLevels()/4);
Bar meanpeakbar2(HORZ_BAR_LEN, gfx.getBrightnessLevels()/4);
Bar meanrmsbar(HORZ_BAR_LEN, gfx.getBrightnessLevels());

Bar* horizontalBars[] = {&peakLbar, &peakRbar, &rmsLbar, &rmsRbar, &meanpeakbar, &meanpeakbar2, &meanrmsbar};

#define NUM_FFT_BARS 10
//uint8_t bucketsplus[] = {4, 4, 8, 12, 16, 20, 28, 40, 56, 80};
// round(logspace(0, log10(15), 10))*4
uint8_t bucketsplus[] = {4, 4, 8, 8, 12, 20, 24, 32, 44, 60};
Bar* fftLbars[NUM_FFT_BARS];
Bar* fftRbars[NUM_FFT_BARS];

void setup() {
  for (uint8_t i = 0; i < NUM_FFT_BARS; i++) {
    fftLbars[i] = new Bar(8, gfx.getBrightnessLevels()/5*3);
    fftRbars[i] = new Bar(8, gfx.getBrightnessLevels()/5*3);
  }

  // ~12 for each FFT and 6 for stereo peak & rms
  AudioMemory(30);

  // Enable the audio shield and set the output volume.
  audioShield.enable();
  audioShield.inputSelect(AUDIO_INPUT_LINEIN);

  gfx.begin(25000000);
  myTimer.begin(myDisplay, 400); // function takes ~50 microseconds

//  Serial.begin(1e6);
}

uint16_t ui16peakL_log[AVERAGE_FOR];
uint16_t ui16peakR_log[AVERAGE_FOR];
uint16_t ui16rmsL_log[AVERAGE_FOR];
uint16_t ui16rmsR_log[AVERAGE_FOR];
uint16_t averageAt = 0;

elapsedMillis framedelay;
elapsedMicros barAnimation;

void loop() {
  if (framedelay > TARGET_FRAMETIME) {
    if (fftL.available() && fftR.available() && peakL.available() &&
        peakR.available() && rmsL.available() && rmsR.available()) {
      framedelay = 0;
      barAnimation = 0;

      uint16_t ui16peakL = log10(9 * peakL.read() + 1) * 65355 * SCALE_PEAKRMS;
      uint16_t ui16peakR = log10(9 * peakR.read() + 1) * 65355 * SCALE_PEAKRMS;
      uint16_t ui16rmsL = log10(9 * rmsL.read() + 1) * 65355 * SCALE_PEAKRMS;
      uint16_t ui16rmsR = log10(9 * rmsR.read() + 1) * 65355 * SCALE_PEAKRMS;

      ui16peakL_log[averageAt] = ui16peakL;
      ui16peakR_log[averageAt] = ui16peakR;
      ui16rmsL_log[averageAt] = ui16rmsL;
      ui16rmsR_log[averageAt] = ui16rmsR;

      unsigned long meanpeak = 0;
      unsigned long meanrms = 0;
      for (unsigned int i = 0; i < AVERAGE_FOR; i++) {
        meanpeak += ui16peakL_log[i] + ui16peakR_log[i];
        meanrms += ui16rmsL_log[i] + ui16rmsR_log[i];
      }
      meanpeak = meanpeak / (2 * AVERAGE_FOR);
      meanrms = meanrms / (2 * AVERAGE_FOR);

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

      peakLbar.setValue(ui16peakL * scalepeak);
      peakRbar.setValue(ui16peakR * scalepeak);
      rmsLbar.setValue(ui16rmsL * scalepeak);
      rmsRbar.setValue(ui16rmsR * scalepeak);

      meanpeakbar.setValue(uint16_t(meanpeak));
      meanpeakbar2.setValue(meanpeak * scalepeak);
      meanrmsbar.setValue(meanrms * scalepeak);

//      if (scalepeak < 1) { // super loud
//        scalepeak = scalepeak * 0.7;
//      }
//      else if (scalepeak == 1) { // no scaling, medium loudness
//        scalepeak = scalepeak * 1.1;
//      }
//      else { // quiets
//        scalepeak = scalepeak * 1.4;
//      }

      // handle FFT data
      uint16_t bat = 0, batnew;
      for (uint8_t i = 0; i < NUM_FFT_BARS; i++) {
        batnew = bat + bucketsplus[i];
//        fftLbars[i]->setValue(log10((9 * fftL.read(bat, batnew - 1) + 1) - (i*0.0005)) * 65354.0);
//        fftRbars[i]->setValue(log10((9 * fftR.read(bat, batnew - 1) + 1) - (i*0.0005)) * 65354.0);
//        fftLbars[i]->setValue(log10(((9 * fftL.read(bat, batnew - 1) + 1) - (i*0.0005)) * scalepeak) * 65354.0);
//        fftRbars[i]->setValue(log10(((9 * fftR.read(bat, batnew - 1) + 1) - (i*0.0005)) * scalepeak) * 65354.0);
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

      averageAt++;
      if (averageAt == AVERAGE_FOR) {
        averageAt = 0;
      }
    }
  }

  if (gfx.wasLastFrameDrawn()) {// only draw next frame when current frame has been displayed fully

  // draw frame
  gfx.fillScreen(BG_BRIGHT);

  //  gfx.drawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);
  //  gfx.drawLine(16, 0, 23, 7,255);
  //  gfx.drawPixel(16, 0, 255);
  //  gfx.drawPixel(0, 1, 255);
  //  gfx.drawPixel(2, 2, 255);
//    gfx.drawLine(8, 5, 8, 7,255);
//    gfx.drawLine(9, 5, 14, 5,255);
//    gfx.drawLine(15, 5, 15, 7,255);

  uint8_t len, remainder, extra = 0;
  for (uint8_t i = 0; i < 7; i++ ) {
    horizontalBars[i]->getBarLength(long(barAnimation), &len, &remainder);
    if (len != 0) {
      gfx.drawLine(0, i + extra, len - 1, i + extra, horizontalBars[i]->getBrightness()); // minus 1 because 0 offset counting pixels
      if (len < HORZ_BAR_LEN) {
        gfx.drawPixel(len, i + extra, remainder);
      }
    }
    if (i == 3) {
      extra = 1;
    }
  }
  // last two bars - mean of FFT? peak FFT?

  for (uint8_t i = 0; i < NUM_FFT_BARS; i++ ) {
    fftLbars[i]->getBarLength(long(barAnimation), &len, &remainder);
    if (len != 0) {
      gfx.drawLine(11 + i, 7 - (len - 1), 11 + i, 7, fftLbars[i]->getBrightness()); // minus 1 because 0 offset counting pixels
      if (len < 8) {
        gfx.drawPixel(11 + i, 7 - len, remainder);
      }
    }
  }

  for (uint8_t i = 0; i < NUM_FFT_BARS; i++ ) { //copied from above just L changed to R
    fftRbars[i]->getBarLength(long(barAnimation), &len, &remainder);
    if (len != 0) {
      gfx.drawLine(22 + i, 7 - (len - 1), 22 + i, 7, fftRbars[i]->getBrightness()); // minus 1 because 0 offset counting pixels
      if (len < 8) {
        gfx.drawPixel(22 + i, 7 - len, remainder);
      }
    }
  }

  gfx.flip();
  }
}
