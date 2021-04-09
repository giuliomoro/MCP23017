// not an actual "render" file at the moment
#include "src/MCP23017.h"
#include <libraries/Encoder/Encoder.h>
#include <stdio.h>
#include <unistd.h>
#include <thread>

Encoder encoder(0, Encoder::ANY);
uint16_t gpio;
MCP23017 mcp(1, 0x22);
uint8_t pins[2] = {0, 1};

void makeBinaryString(char* str, uint16_t data)
{
	for(unsigned int n = 0; n < sizeof(data) * 8; ++n, ++str)
		*str = (data & (1 << n)) ? '1' : '0';
	*str = '\0';
}

void processEnc()
{
	while(1)
	{
		gpio = mcp.readGPIOAB();
		encoder.process(gpio & (1 << pins[0]), gpio & (1 << pins[1]));
	}
}
// Input #0 is on pin 21 so connect a button or switch from there to ground
int main() {
	std::thread th(processEnc);
	if(!mcp.openI2C())
	{
		fprintf(stderr, "Failed to open device\n");
		return -1;
	}
	mcp.pinMode(0, MCP23017::INPUT);
	mcp.pullUp(0, MCP23017::HIGH);  // turn on a 100K pullup internally
	for(unsigned int n = 0; n < 16; ++n)
		mcp.pinMode(n, MCP23017::INPUT);
	int ss = 100;
	char stars[ss + 1];
	for(unsigned int n = 0; n < ss; ++n)
	{
		stars[n] = '*';
	}
	stars[ss] = '\0';
	while(true){
		static int oldRot = 0;
		static uint16_t oldGpio = 0;
		int rot = encoder.get();
		if(gpio != oldGpio || oldRot != rot)
		{
			char str[17];
			makeBinaryString(str, gpio);
			int numStars = rot + 1;
			if(numStars > ss)
				numStars = ss;
			else if (numStars < 0)
				numStars = 0;
			printf("%s: %d %d %.*s\n", str, rot, numStars, numStars, stars);
		}
		oldRot = rot;
		oldGpio = gpio;
		usleep(50000);
	}
	return 0;
}
