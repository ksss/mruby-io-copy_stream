FROM alpine:3.6
RUN apk update && apk upgrade && apk add --no-cache git ruby gcc bison musl-dev make
RUN git clone --depth 1 https://github.com/mruby/mruby.git
