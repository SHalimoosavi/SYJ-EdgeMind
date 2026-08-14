# SYJ EdgeMind Architecture

## Overview

SYJ EdgeMind is a native, offline-first local AI runtime designed for
CPU-bound inference on resource-constrained devices.

The architecture intentionally separates the inference core from platform
integration.

```text
                         SYJ EdgeMind
                              |
                +-------------+-------------+
                |                           |
          Windows CLI                  iOS App/Core
                |                           |
                +-------------+-------------+
                              |
                        Stable C API
                     <- src/api/
                              |
                    SYJ Edge Runtime
                              |
          +-------------------+-------------------+
          |                   |                   |
       Runtime            Context             Inference
       Config            Manager               Engine
          |                   |                   |
      src/core/         src/context/       src/inference/
                              |                   |
                              +---------+---------+
                                        |
                                    Tokenizer
                                  src/tokenizer/
                                        |
                                    llama.cpp
                                        |
                                GGUF Quantized Model
                                        |
                              CPU / RAM / mmap
