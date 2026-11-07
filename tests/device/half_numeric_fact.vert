#version 450
#ifndef BASE
#define BASE 12
#endif
layout(set=0,binding=BASE) uniform samplerBuffer instanceWords;
layout(set=0,binding=BASE+6) uniform samplerBuffer halfScalar;
layout(set=0,binding=BASE+12) uniform samplerBuffer lookupWords;
layout(location=0) out uvec4 rawWords;
void main() {
    rawWords = floatBitsToUint(texelFetch(instanceWords, 0));
    gl_Position = texelFetch(lookupWords, int(rawWords.x & 0u)) + texelFetch(halfScalar, 0);
}
