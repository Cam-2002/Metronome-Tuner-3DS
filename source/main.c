#include <stdio.h>
#include <3ds.h>
#include <math.h>
#include <string.h>
#include <citro2d.h>

#define SCREENWIDTH 400
#define SCREENHEIGHT 240
#define SAMPLERATE 48000
#define SAMPLESPERBUF (SAMPLERATE/20)
#define BYTESPERSAMPLE 4

Thread metThread, inputThread, toneThread, metDisplayThread;
Handle metHandle, inputHandle, toneHandle, metDisplayHandle;
bool runThread = true, metEnable = false, metPause = false, toneFreqMode = false, metDisplayEnable = true;
int bpm = 120, optionSelected = 0, toneOption = 0, beatsPerMeasure = 4, beat = 0;
float toneFreq[5] = {440.0,550.0,660.0,825.0,990.0};
bool toneEn[5] = {false, false, false, false, false}, toneBufAlt[5] = {false, false, false, false, false};
int toneWave[5] = {0,0,0,0,0}, toneOffset[5] = {0,0,0,0,0}, toneMidi[5] = {69,73,76,80,83};
C3D_RenderTarget* gfxTopScreen;
u32 colors[8];
float tappedBPM = 0.0;
ndspWaveBuf waveBuf, toneBuf[10]; //buffer that the ndsp engine uses


//audioBuffer = buffer for ndsp, offset = wave offset, size = size of the buffer, frequency = frequency.
//waveform = sin (0), saw (1), square (2), triangle (3)
void fillAudioBuffer(void *audioBuffer, size_t offset, size_t size, int frequency, int waveform) {
	u32 *dest = (u32*)audioBuffer; //will set audio information into the destination buffer
	for (int i=offset; i<size+offset; i++) { //for each piece of data in the buffer
		s16 sample;
		if(waveform == 1) sample = INT16_MAX * (i%(SAMPLERATE/frequency))*frequency/SAMPLERATE; //calculate what the data should be by saw wave
		else if(waveform == 2) sample = ((float)(i%(SAMPLERATE/frequency))*frequency/SAMPLERATE < 0.5f)?INT16_MAX:0; //square wave (last float is percentage of activation)
		else if(waveform == 3) sample = INT16_MAX * (4.0*frequency/SAMPLERATE) * (i-(SAMPLERATE*floor((2.0*frequency*i/SAMPLERATE)+0.5)/(2*frequency)))*pow(-1,floor((2.0*frequency*i/SAMPLERATE)+0.5)); //triangle wave
		else sample = INT16_MAX * sin(frequency*(2*M_PI)*(i)/SAMPLERATE); //sine wave
		dest[i-offset] = (sample<<16) | (sample & 0xffff); //set the data
	}
	DSP_FlushDataCache(audioBuffer,size);
}

//Thread that triggers on every metronome pulse
void metFuncThread(void *arg){
	int frequency = 880;
	while(runThread){
		svcClearTimer(metHandle);
		svcWaitSynchronization(metHandle, U64_MAX);
		if(metEnable && !metPause){
			if(metDisplayEnable) svcSignalEvent(metDisplayHandle);
			//First beat of measure or not?
			if(beat%beatsPerMeasure==0) frequency = 880*(4.0/3);
			else frequency = 880;

			fillAudioBuffer(waveBuf.data_pcm16, 0, waveBuf.nsamples,frequency,1);
			ndspChnWaveBufAdd(0, &waveBuf);

			svcSleepThread((long int)1000000000*45.0/bpm);

			beat++;
		}else{
			beat = 0;
		}
	}
}

void metDisplayFuncThread(void *arg){
	while(runThread){
		svcWaitSynchronization(metDisplayHandle, U64_MAX);
		svcClearEvent(metDisplayHandle);
		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		C2D_TargetClear(gfxTopScreen, colors[7]);
		C2D_SceneBegin(gfxTopScreen);
		C2D_DrawRectangle(20,20,0.5,SCREENWIDTH-40,SCREENHEIGHT-40,colors[7],colors[7],colors[7],colors[7]);
		if(beat%beatsPerMeasure==0) C2D_DrawRectangle(20+((beat%beatsPerMeasure)*(SCREENWIDTH-40)/beatsPerMeasure),20,0.5,(SCREENWIDTH-40)/beatsPerMeasure,SCREENHEIGHT-40,colors[0],colors[0],colors[0],colors[0]);
		else C2D_DrawRectangle(20+((beat%beatsPerMeasure)*(SCREENWIDTH-40)/beatsPerMeasure),20,0.5,(SCREENWIDTH-40)/beatsPerMeasure,SCREENHEIGHT-40,colors[0],colors[0],colors[0],colors[0]);
		C3D_FrameEnd(0);
	}
}

void toneGenThread(void *arg){
	while(runThread){
		svcClearTimer(toneHandle);
		int enabled = 0;
		for(int i=0; i<5; i++) if(toneEn[i]) enabled++;
		if(enabled == 0) enabled++;
		float volume[12] = {1.0/((float)enabled+0.02),1.0/enabled,0,0,0,0,0,0,0,0,0,0}; 
		for(int i=0; i<5; i++){
			int ti = (i*2)+(toneBufAlt[i]?1:0);
			ndspChnSetMix(i+1, volume);
			if(toneEn[i] && (toneBuf[ti].status == NDSP_WBUF_DONE || toneBuf[ti].status == NDSP_WBUF_FREE)){
				fillAudioBuffer(toneBuf[ti].data_pcm16, toneBuf[ti].nsamples*toneOffset[i], toneBuf[ti].nsamples, toneFreq[i], toneWave[i]);
				ndspChnWaveBufAdd(i+1, &toneBuf[ti]);
				toneOffset[i] += 1;
				toneBufAlt[i] = !toneBufAlt[i];
			}
		}
		svcWaitSynchronization(toneHandle, U64_MAX);
	}
}

//Thread that triggers at 1000 Hz to determine inputs, also used for BPM tapping feature
void inputFuncThread(void *arg){
	int frame = 0, beats = 0, metOff = 0;
	long int millis = -1; //When bpm tapping is not reset, will tick up at 1000Hz
	while(runThread){
		svcClearTimer(inputHandle);
		frame++;
		hidScanInput();

		u32 kHeld = hidKeysHeld();
		u32 kDown = hidKeysDown();
		u32 kUp   = hidKeysUp();

		if(metOff > 1){
			metPause = true;
			metOff--;
		}
		else if(metOff == 1){
			metPause = false;
			metOff--;
		}

		//Sets the BPM with UP and DOWN arrow keys
		//TODO: replace code with a full menu selection thing
		if(kHeld & KEY_RIGHT && frame%10==0 && optionSelected == 0){
			if(bpm < 999) bpm++;
			metOff = 250;
		}
		if(kHeld & KEY_LEFT && frame%10==0 && optionSelected == 0){
			if(bpm>1) bpm--;
			metOff = 250;
		}
		int nonToneMenuItems = 4;
		if(kDown & KEY_RIGHT){
			if(optionSelected == 1) metEnable = !metEnable;
			if(optionSelected == 2) metDisplayEnable = !metDisplayEnable;
			if(optionSelected == 3 && beatsPerMeasure<99) beatsPerMeasure++;
			if(optionSelected > 3){
				if(toneOption==0) toneEn[optionSelected-nonToneMenuItems] = !toneEn[optionSelected-nonToneMenuItems];
				if(toneOption==1 && toneWave[optionSelected-nonToneMenuItems]<3) toneWave[optionSelected-nonToneMenuItems]++;
				if(toneOption==2){
					if(toneMidi[optionSelected-nonToneMenuItems]<127) toneMidi[optionSelected-nonToneMenuItems]++;
					toneFreq[optionSelected-nonToneMenuItems]=440*pow(2,((float)toneMidi[optionSelected-nonToneMenuItems]-69)/12);
				} 
				if(toneOption==3){
					if(toneMidi[optionSelected-nonToneMenuItems]<116) toneMidi[optionSelected-nonToneMenuItems]+=12;
					toneFreq[optionSelected-nonToneMenuItems]=440*pow(2,((float)toneMidi[optionSelected-nonToneMenuItems]-69)/12);
				}
				if(toneOption>3){
					if(toneFreq[optionSelected-nonToneMenuItems]+pow(10,8-toneOption)<22050) toneFreq[optionSelected-nonToneMenuItems]+=pow(10,8-toneOption);
				}
			} 
		}
		if(kDown & KEY_LEFT){
			if(optionSelected == 1) metEnable = !metEnable;
			if(optionSelected == 2) metDisplayEnable = !metDisplayEnable;
			if(optionSelected == 3 && beatsPerMeasure>1) beatsPerMeasure--;
			if(optionSelected > 3){
				if(toneOption==0) toneEn[optionSelected-nonToneMenuItems] = !toneEn[optionSelected-nonToneMenuItems];
				if(toneOption==1 && toneWave[optionSelected-nonToneMenuItems]>0) toneWave[optionSelected-nonToneMenuItems]--;
				if(toneOption==2){
					if(toneMidi[optionSelected-nonToneMenuItems]>0) toneMidi[optionSelected-nonToneMenuItems]--;
					toneFreq[optionSelected-nonToneMenuItems]=440*pow(2,((float)toneMidi[optionSelected-nonToneMenuItems]-69)/12);
				} 
				if(toneOption==3){
					if(toneMidi[optionSelected-nonToneMenuItems]>11) toneMidi[optionSelected-nonToneMenuItems]-=12;
					toneFreq[optionSelected-nonToneMenuItems]=440*pow(2,((float)toneMidi[optionSelected-nonToneMenuItems]-69)/12);
				}
				if(toneOption>3){
					if(toneFreq[optionSelected-nonToneMenuItems]-pow(10,8-toneOption)>4) toneFreq[optionSelected-nonToneMenuItems]-=pow(10,8-toneOption);
				}
			} 
		}

		if(optionSelected>2){
			if(kDown & KEY_L){
				if(toneOption > 0) toneOption--;
			}
			if(kDown & KEY_R){
				if(toneOption < 10) toneOption++;
			}
		}else toneOption = 0;

		if((kUp & KEY_RIGHT || kUp & KEY_LEFT) && optionSelected == 0){
			svcSetTimer(metHandle,0,lround((long int)1000000000*60.0/bpm));
		}

		//When pressing X, add a beat to the BPM calculator then calculate the BPM
		if(kDown & KEY_X){
			if(millis < 0) millis = 0;
			else tappedBPM = beats/((float)millis/60000);
			beats++;

		}
		//When pressing Y, reset the BPM calculator
		if(kDown & KEY_Y){
			millis = -1;
			tappedBPM = 0.0;
			beats = 0;
		}

		//Menu navigation
		if(kDown & KEY_UP){
			if(optionSelected>0) optionSelected--;
			else optionSelected = 8;
		}
		if(kDown & KEY_DOWN){
			if(optionSelected<8) optionSelected++;
			else optionSelected = 0;
		}

		//Enable / Disable metronome
		if(kDown & KEY_SELECT){
			if(metEnable){
				metEnable = false;
			}else{
				metOff = 250;
				svcSetTimer(metHandle,0,lround((long int)1000000000*60.0/bpm));
				metEnable = true;
			}
		}

		//Stop everything
		if(kDown & KEY_START){
			runThread = false;
		}

		//Increase millis
		if(millis>-1){
			millis+=5;
		}
		svcWaitSynchronization(inputHandle, U64_MAX);
	}
}

int main(int argc, char** argv){
	//Initialize graphics
	gfxInitDefault();
	consoleInit(GFX_BOTTOM, NULL);
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
	C2D_Prepare();
	gfxTopScreen = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);

	for(int i=0; i<8; i++){
		int shade = 255*((1.0/i)-(1.0/7));
		//int shade = 0xFF;
		colors[i] = C2D_Color32(shade,shade,shade,0xFF);
	}

	//Initialize audio engine
	ndspInit();
	ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	ndspSetClippingMode(NDSP_CLIP_NORMAL);
	float volume[12] = {1,1,0,0,0,0,0,0,0,0,0,0}; 
	for(int i=0; i<6; i++){
		ndspChnSetInterp(i, NDSP_INTERP_LINEAR); //linear interpolation on channel i
		ndspChnSetRate(i, SAMPLERATE); //sets sample rate to 48000 kHz
		ndspChnSetFormat(i, NDSP_FORMAT_STEREO_PCM16); //use stereo pcm16
		ndspChnSetMix(i, volume); //sets front left/right volume to full
	}

	//Set up audio buffer
	u32 *audioBuffer = (u32*)linearAlloc(SAMPLESPERBUF*BYTESPERSAMPLE*11); //buffer of audio information
	waveBuf.data_vaddr = &audioBuffer[0]; //define start of audio information
	waveBuf.nsamples = SAMPLESPERBUF; //define amount of audio information
	for(int i=0; i<10; i++){
		toneBuf[i].data_vaddr = &audioBuffer[SAMPLESPERBUF*(i+1)];
		toneBuf[i].nsamples = SAMPLESPERBUF;
	}

	//Play 440 Hz for 1/20 of a second
	//fillAudioBuffer(audioBuffer, 0, SAMPLESPERBUF, 440, 3);
	//ndspChnWaveBufAdd(0, &waveBuf);


	//Set up timer loops
	svcCreateTimer(&metHandle,0);
	svcCreateTimer(&inputHandle,0);
	svcCreateTimer(&toneHandle,0);
	svcCreateEvent(&metDisplayHandle,0);
	metThread = threadCreate(metFuncThread, 0, 4096, 0x30, -2, true);
	inputThread = threadCreate(inputFuncThread, 0, 4096, 0x31, -2, true);
	toneThread = threadCreate(toneGenThread, 0, 4096, 0x32, -2, true);
	metDisplayThread = threadCreate(metDisplayFuncThread, 0, 4096, 0x33, -2, true);
	svcSetTimer(inputHandle,0,1000000000/200); // 200Hz polling rate for input
	svcSetTimer(toneHandle,0,1000000000/50); // 50Hz polling rate for tone generator
	svcSetTimer(metHandle,0,lround((long int)1000000000*60.0/bpm));

	printf("\x1b[2;2HMetronome by KAM");

	char waveforms[4][4] = {"Sin","Saw","Sqr","Tri"};
	char midiNotes[12][3] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};

	//Text printing loop
	while(aptMainLoop() && runThread){
		gspWaitForVBlank();

		printf("\x1b[4;2HTempo (BPM) =  \x1b[%dm%3d\x1b[0m    ", optionSelected==0?35:0, bpm);
		printf("\x1b[5;2HMetronome   =  \x1b[%dm%s\x1b[0m    ", optionSelected==1?35:0, metEnable?" ON":"OFF");
		printf("\x1b[6;2HVisual Disp =  \x1b[%dm%s\x1b[0m    ", optionSelected==2?35:0, metDisplayEnable?" ON":"OFF");
		printf("\x1b[7;2H# of Beats  =   \x1b[%dm%2d\x1b[0m    ", optionSelected==3?35:0, beatsPerMeasure);

		for(int i=0; i<5; i++){
			char freqBuf[9];
			sprintf(freqBuf,"%8.2f",toneFreq[i]);
			for(int i=0; i<9; i++) if(freqBuf[i]==' ') freqBuf[i] = '_';
			printf("\x1b[%d;2HTone %d) \x1b[%dm%s \x1b[%dm%s \x1b[%dm%-2s\x1b[%dm%-2d\x1b[0m \x1b[%dm%c\x1b[%dm%c\x1b[%dm%c\x1b[%dm%c\x1b[%dm%c\x1b[0m.\x1b[%dm%c\x1b[%dm%c\x1b[0m", 9+i, 1+i,
				(optionSelected==4+i && toneOption==0)?35:0, toneEn[i]?"ON ":"OFF",
				(optionSelected==4+i && toneOption==1)?35:0, waveforms[toneWave[i]],
				(optionSelected==4+i && toneOption==2)?35:0, midiNotes[toneMidi[i]%12],
				(optionSelected==4+i && toneOption==3)?35:0, (int)floor((float)toneMidi[i]/12)-1,
				(optionSelected==4+i && toneOption==4)?35:0, freqBuf[0],
				(optionSelected==4+i && toneOption==5)?35:0, freqBuf[1],
				(optionSelected==4+i && toneOption==6)?35:0, freqBuf[2],
				(optionSelected==4+i && toneOption==7)?35:0, freqBuf[3],
				(optionSelected==4+i && toneOption==8)?35:0, freqBuf[4],
				(optionSelected==4+i && toneOption==9)?35:0, freqBuf[6],
				(optionSelected==4+i && toneOption==10)?35:0, freqBuf[7]);
		}

		printf("\x1b[16;2HUse X to tap the beat, and Y to reset.\x1b[18;2HTapped BPM = \x1b[35m%.2f\x1b[0m          ",tappedBPM);

		if(!metDisplayEnable || !metEnable){
			C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
			C2D_TargetClear(gfxTopScreen, colors[7]);
			C2D_SceneBegin(gfxTopScreen);
			C2D_DrawRectangle(20,20,0.6,SCREENWIDTH-40,SCREENHEIGHT-40,colors[7],colors[7],colors[7],colors[7]);
			C3D_FrameEnd(0);
		}

		gfxFlushBuffers();
		//gfxSwapBuffers();
	}

	//Close everything
	runThread = false;
	svcSignalEvent(metHandle);
	svcSignalEvent(inputHandle);
	svcCloseHandle(metHandle);
	svcCloseHandle(inputHandle);
	ndspExit();
	linearFree(audioBuffer);

	gfxExit();
	return 0;
}