#!/bin/bash

USE_PORTFOLIO=""
USE_EXCHANGE=""
VERBOSE_BOOK_BUILDER="OFF"
VERBOSE_STRATEGY="OFF"

# Parse command-line arguments
while [[ $# -gt 0 ]]
do
    key="$1"

    case $key in
        -p|--portfolio)
        USE_PORTFOLIO="$2"
        shift # past argument
        shift # past value
        ;;
        -e|--exchange)
        USE_EXCHANGE="$2"
        shift # past argument
        shift # past value
        ;;
        --verbose-book-builder)
        VERBOSE_BOOK_BUILDER="ON"
        shift # past argument
        ;;
        --verbose-strategy)
        VERBOSE_STRATEGY="ON"
        shift # past argument
        ;;
        *)    # unknown option
        echo "Unknown option: $key"
        exit 1
        ;;
    esac
done

# Check if portfolio and exchange options are set
if [[ -z "$USE_PORTFOLIO" ]]; then
    echo "Error: An option must be specified for portfolio using -p or --portfolio."
    exit 1
fi

if [[ -z "$USE_EXCHANGE" ]]; then
    echo "Error: An option must be specified for exchange using -e or --exchange."
    exit 1
fi

# Remove the build directory
rm -rf build

# Recreate the build directory
mkdir build

# Change to the build directory
cd build || exit

# Run cmake
cmake -D"$USE_PORTFOLIO"=ON -D"$USE_EXCHANGE"=ON -DVERBOSE_BOOK_BUILDER="$VERBOSE_BOOK_BUILDER" -DVERBOSE_STRATEGY="$VERBOSE_STRATEGY" ..

# Run make
make
