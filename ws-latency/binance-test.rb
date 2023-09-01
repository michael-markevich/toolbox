#!/usr/bin/env ruby

require 'websocket-client-simple'
require 'json'
require 'time'

symbol = "btcusdt"
stream_url = "wss://stream.binance.com/ws/#{symbol}@trade"

latency_samples = []

ws = WebSocket::Client::Simple.connect(stream_url)

ws.on :message do |msg|
    data = JSON.parse(msg.data)
    if data["e"] == "trade"
        received_timestamp = Time.now
        request_timestamp = data["T"]
        latency = (received_timestamp - Time.at(request_timestamp / 1000.0)).to_f
        latency_samples << latency
        puts "Latency: #{latency} seconds"
    end
end

ws.on :open do
    payload = {
        method: "SUBSCRIBE",
        params: ["#{symbol}@trade"],
        id: 1
       }.to_json
    ws.send(payload)
end

begin
    loop do
        sleep 1
    end
rescue Interrupt
    average_latency = latency_samples.reduce(:+) / latency_samples.length
    puts "Average Latency: #{average_latency} seconds"
end
