#!/bin/bash

if [ -z "$1" ]; then
    echo "Usage: $0 <message>"
    exit 1
fi

curl -X POST -H "Content-Type: application/json" -d "{\"username\":\"cURL\",\"text\":\"$1\"}" localhost:7815/message
