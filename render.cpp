#include <Bela.h>
#include "src/MCP23017.h"
#include <libraries/Encoder/Encoder.h>
#include <libraries/Oscillator/Oscillator.h>
#include <libraries/OnePole/OnePole.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <thread>
#include <vector>
#include <cmath>

std::vector<Oscillator> oscillators;
std::vector<OnePole> smoothers;
std::vector<Encoder> encoders;
const unsigned int kEncodersPerBoard = 4;
std::vector<std::array<uint8_t, 2>> pinsPairs = {
	{{6, 5}},
	{{3, 2}},
	{{9, 10}},
	{{12, 13}},
};

int i2cBus = 1;
std::vector<uint8_t> i2cAddresses = {
	0x21,
	0x22,
	0x26,
	0x27,
};

std::vector<uint16_t> oldGpios;
std::vector<uint16_t> gpios;
std::vector<MCP23017> mcps;

void makeBinaryString(char* str, uint16_t data)
{
	for(unsigned int n = 0; n < sizeof(data) * 8; ++n, ++str)
		*str = (data & (1 << n)) ? '1' : '0';
	*str = '\0';
}

void processEnc(void*)
{
	std::vector<uint16_t> oldGpios = gpios;
	while(!gShouldStop)
	{
		for(unsigned int m = 0; m < mcps.size(); ++m)
		{
			MCP23017& mcp = mcps[m];
			uint16_t& oldGpio = oldGpios[m];
			uint16_t& gpio = gpios[m];
			// we always read from INTCAP (the state of the GPIO when the
			// interrupt was triggered). This resets the interrupt and
			// allows us to see which pin toggled first (useful for encoders!)
			gpio = mcp.readINTCAPAB();
			for(unsigned int n = 0; n < pinsPairs.size(); ++n)
			{
				auto& pins = pinsPairs[n % kEncodersPerBoard];
				encoders[n + m * kEncodersPerBoard].process(gpio & (1 << pins[0]), gpio & (1 << pins[1]));
			}
			if(oldGpio != gpio)
			{
				// we caught the device as an interrupt had just occurred.
				// add a little "debouncing" delay before reading again
				// TODO: do not hold back reading back other devices while debouncing one device
				usleep(500);
				// read and ignore in order to clear any interrupt due to bounces that
				// may have occurred in the meantime.
				mcp.readINTCAP(0);
			}
		}
	}
}

void printEnc(void*)
{
	int ss = 20;
	char stars[ss + 1];
	char spaces[ss + 1];
	memset(stars, '*', ss);
	memset(spaces, ' ', ss);
	spaces[ss] = stars[ss] = '\0';
	std::vector<int> oldRots(encoders.size());
	std::vector<uint16_t> oldGpios = gpios;
	while(!gShouldStop){
		for(unsigned int m = 0; m < mcps.size(); ++m)
		{
			uint16_t& gpio = gpios[m];
			uint16_t& oldGpio = oldGpios[m];
			bool shouldPrint = false;
			if(gpio != oldGpio)
				shouldPrint = true;
			oldGpio = gpio;
			unsigned int encStart = m * kEncodersPerBoard;
			unsigned int encEnd = (1 + m) * kEncodersPerBoard;
			for(unsigned int n = encStart; n < encEnd; ++n)
			{
				int& oldRot = oldRots[n];
				int rot = encoders[n].get();
				if(oldRot != rot)
					shouldPrint = true;
				oldRot = rot;
			}
			if(shouldPrint)
			{
				char str[17];
				makeBinaryString(str, gpio);
				printf("[%d] %s: ", m, str);
				for(unsigned int n = encStart; n < encEnd; ++n)
				{
					int rot = encoders[n].get();
					int numStars = rot + 1;
					while(numStars < 0)
						numStars += ss;
					numStars %= ss;
					printf("%4d %.*s%.*s", rot, numStars, stars, ss - numStars, spaces);
				}
				printf("\n");
			}
			usleep(50000);
		}
	}
}

bool setup(BelaContext* context, void*)
{
	mcps.reserve(i2cAddresses.size()); // ensure no allocation happens in the below loop.
	for(unsigned int c = 0; c < i2cAddresses.size(); ++c)
	{
		mcps.emplace_back(i2cBus, i2cAddresses[c]);
		if(!mcps.back().openI2C())
		{
			fprintf(stderr, "Failed to open device on bus %d, address %#x\n", i2cBus, i2cAddresses[c]);
			mcps.erase(mcps.end() - 1, mcps.end());
			continue;
		}
		MCP23017& mcp = mcps.back();
		gpios.push_back(0);
		for(unsigned int n = 0; n < 16; ++n)
		{
			mcp.pinMode(n, MCP23017::INPUT);
			mcp.pullUp(n, MCP23017::HIGH);  // turn on a 100K pullup internally
			// we set up interrupts so we can read the INTCAP register
			mcp.setupInterrupts(true, false, MCP23017::HIGH);
			for(unsigned int n = 0; n < 16; ++n)
				mcp.setupInterruptPin(n, MCP23017::CHANGE);
		}
	}
	if(!mcps.size())
	{
		fprintf(stderr, "No device detected\n");
		return false;
	} else {
		printf("%d encoder boards detected\n", mcps.size());
	}
	for(unsigned int n = 0; n < mcps.size() * kEncodersPerBoard; ++n)
	{
		oscillators.push_back({context->audioSampleRate});
		smoothers.push_back({context->audioSampleRate / context->audioFrames, context->audioSampleRate});
		encoders.push_back({0, Encoder::ANY});
	}
	Bela_runAuxiliaryTask(processEnc, 90);
	Bela_runAuxiliaryTask(printEnc, 0);
	return true;
}

void render(BelaContext* context, void*)
{
	float freqs[encoders.size()];
	// get encoder readings once per block and compute target frequency
	for(unsigned int n = 0; n < encoders.size(); ++n)
		freqs[n] = 440 * powf(2, encoders[n].get() / 24.f);

	for(unsigned int n = 0; n < context->audioFrames; ++n)
	{
		float out[encoders.size()];
		memset(out, 0, sizeof(out));
		for(unsigned int v = 0; v < encoders.size(); ++v)
		{
			// smooth towards target frequency
			freqs[v] = smoothers[v].process(freqs[v]);
			out[v] += oscillators[v].process(freqs[v]);
		}
		float channelOut = 0;
		// downmix
		for(unsigned int v = 0; v < encoders.size(); ++v)
			channelOut += out[v];
		for(unsigned int c = 0; c < context->audioOutChannels; ++c)
		{
			audioWrite(context, n, c, channelOut / encoders.size());
		}
	}
}

void cleanup(BelaContext* context, void*)
{
}
