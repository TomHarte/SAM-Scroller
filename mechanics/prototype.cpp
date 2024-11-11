#include <iostream>

int16_t current[2], previous[2];

void progress() {
	int16_t difference[2] = {
		int16_t(current[0] - previous[0]), 
		int16_t( current[1] - previous[1]),
	};
	
	// First try: subtract half, for agressive friction.
	difference[0] -= difference[0] >> 1;
	difference[1] -= difference[1] >> 1;

	// Apply hard stop if motion is below 1/8th.
	if(difference[0] > -32 && difference[0] < 32) difference[0] = 0;
	if(difference[1] > -32 && difference[1] < 32) difference[1] = 0;

	// Update previous.
	previous[0] = current[0];
	previous[1] = current[1];

	// Progress.
	current[0] += difference[0];
	current[1] += difference[1];
}

int frames_to_stop(int motion) {
	current[0] = motion << 8;
	previous[0] = 0;
	
	int c = 0;
	while(current[0] != previous[0]) {
		printf("%0.2f ", float(current[0]) / 256.0);
		progress();
		++c;
	}
	printf("\n");

	return c;
}

int frames_to_start(int16_t target, int16_t delta) {
	current[0] = previous[0] = 0;
	
	int c = 0;
	int max = 100;
	while((current[0] - previous[0]) < target) {
		progress();
		current[0] += delta;
		printf("%0.2f ", float(current[0] - previous[0]) / 256.0);
		++c;
		
		--max;
		if(!max) break;
	}
	printf("\n");
	
	return c;
}
	

int main(int argc, char *argv[]) {
	printf("Frames to stop: %d\n", frames_to_stop(2));
	printf("Frames to start: %d\n", frames_to_start(2 << 8, 256));
	printf("Gravity topper: %d\n", frames_to_start(4 << 8, 512));

	return 0;
}