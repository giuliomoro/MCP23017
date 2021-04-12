// not an actual "render" file at the moment
#include "src/MCP23017.h"
#include <libraries/Encoder/Encoder.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <thread>
#include <vector>

std::vector<Encoder> encoders;
std::vector<std::array<uint8_t, 2>> pinsPairs = {
	{{0, 1}},
	{{2, 3}},
	{{4, 5}},
	{{6, 7}},
};

uint16_t gpio;
MCP23017 mcp(1, 0x22);

void makeBinaryString(char* str, uint16_t data)
{
	for(unsigned int n = 0; n < sizeof(data) * 8; ++n, ++str)
		*str = (data & (1 << n)) ? '1' : '0';
	*str = '\0';
}

void processEnc()
{
	// we set up interrupts but don't actually use the interrupt pins.
	mcp.setupInterrupts(true, false, MCP23017::HIGH);
	for(unsigned int n = 0; n < 16; ++n)
		mcp.setupInterruptPin(n, MCP23017::CHANGE);
	while(1)
	{
		// we always read from INTCAP (the state of the GPIO when the
		// interrupt was triggered). This resets the interrupt and
		// allows us to see which pin toggled first (useful for encoders!)
		static uint16_t oldGpio;
		gpio = mcp.readINTCAP(0);
		for(unsigned int n = 0; n < pinsPairs.size(); ++n)
		{
			auto& pins = pinsPairs[n];
			Encoder& encoder = encoders[n];
			encoder.process(gpio & (1 << pins[0]), gpio & (1 << pins[1]));
		}
		if(oldGpio != gpio)
		{
			// we caught the device as an interrupt had just occurred.
			// add a little "debouncing" delay before reading again
			usleep(500);
			// read and ignore in order to clear any interrupt that
			// may have come up in the meantime.
			mcp.readINTCAP(0);
		}
	}
}
// Input #0 is on pin 21 so connect a button or switch from there to ground
int main() {
	for(unsigned int n = 0; n < pinsPairs.size(); ++n)
		encoders.push_back({0, Encoder::ANY});
	std::thread th(processEnc);
	if(!mcp.openI2C())
	{
		fprintf(stderr, "Failed to open device\n");
		return -1;
	}
	for(unsigned int n = 0; n < 16; ++n)
	{
		mcp.pinMode(n, MCP23017::INPUT);
		mcp.pullUp(n, MCP23017::HIGH);  // turn on a 100K pullup internally
	}
	int ss = 20;
	char stars[ss + 1];
	char spaces[ss + 1];
	memset(stars, '*', ss);
	memset(spaces, ' ', ss);
	spaces[ss] = stars[ss] = '\0';
	std::vector<int> oldRots(encoders.size());
	uint16_t oldGpio = 0;
	while(true){
		bool shouldPrint = false;
		if(gpio != oldGpio)
			shouldPrint = true;
		oldGpio = gpio;
		for(unsigned int n = 0; n < encoders.size(); ++n)
		{
			Encoder& encoder = encoders[n];
			int& oldRot = oldRots[n];
			int rot = encoder.get();
			if(oldRot != rot)
				shouldPrint = true;
			oldRot = rot;
		}
		if(shouldPrint)
		{
			char str[17];
			makeBinaryString(str, gpio);
			printf("%s: ", str);
			for(auto& e : encoders)
			{
				int rot = e.get();
				int numStars = rot + 1;
				while(numStars < 0)
					numStars += ss;
				numStars %= ss;
				printf("%4d %4d %.*s%.*s", rot, numStars, numStars, stars, ss - numStars, spaces);
			}
			printf("\n");
		}
		usleep(50000);
	}
	return 0;
}
