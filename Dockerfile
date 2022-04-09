FROM fuzzers/afl:2.52

# Deps
RUN apt-get update 
RUN apt install -y build-essential wget git clang cmake
RUN git clone https://github.com/xiph/ogg.git
WORKDIR /ogg
RUN cmake -DBUILD_SHARED_LIBS=1 .
RUN make
RUN make install

WORKDIR /

RUN git clone https://github.com/xiph/flac.git
WORKDIR /flac
RUN cmake -DBUILD_SHARED_LIBS=1 .
RUN make
RUN make install

# Setup harness
COPY ./fuzzers/flacfuzz.c .
COPY ./include/share/compat.h .
RUN afl-clang -I/usr/local/include -lFLAC -fno-inline flacfuzz.c -o /flac_fuzz

RUN mkdir -p /in
RUN wget https://freewavesamples.com/files/Ensoniq-ZR-76-01-Dope-77.wav && mv Ensoniq-ZR-76-01-Dope-77.wav sampleWav.wav
RUN wget https://download.samplelib.com/jpeg/sample-clouds-400x300.jpg && mv sample-clouds-400x300.jpg sampleImg.jpeg
#RUN mv sampleWav.wav /in
RUN mv sampleImg.jpeg /in

RUN echo core >/proc/sys/kernel/core_pattern

# fuzz
ENTRYPOINT ["afl-fuzz", "-i", "/in", "-o", "/out"]
CMD ["/flac_fuzz", "@@"]
