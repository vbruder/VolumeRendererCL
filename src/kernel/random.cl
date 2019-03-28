 
unsigned int ParallelRNG( unsigned int x )
{
	unsigned int value = x;

	value = (value ^ 61) ^ (value>>16);
	value *= 9;
	value ^= value << 4;
	value *= 0x27d4eb2d;
	value ^= value >> 15;

	return value;
}

unsigned int ParallelRNG2( unsigned int x,  unsigned int y )
{
	unsigned int value = ParallelRNG(x);
	value = ParallelRNG( y ^ value );
	return value;
}

unsigned int ParallelRNG3( unsigned int x,  unsigned int y,  unsigned int z )
{
	unsigned int value = ParallelRNG(x);
	value = ParallelRNG( y ^ value );
	value = ParallelRNG( z ^ value );
	return value;
}

// random between 0.0 and 1.0
float trigRNG2(int2 id)
{
    float iptr;
    return fract(sin(dot(convert_float2(id), (float2)(12.9898f, 78.233f))) * 43758.5453f, &iptr);
}


// map -1.0 - 1.0 onto 0-255
float  map256(float v)	
{ 
	return ((127.5f * v) + 127.5f); 
}

float mapUintFloat(uint v)
{
    return (float)(v) / (float)(UINT_MAX);
}
