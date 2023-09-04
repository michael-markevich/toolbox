#!/usr/bin/env ruby
# frozen_string_literal: true

require 'net/http'
require 'uri'
require 'json'

# Define the Binance API endpoint
binance_api_url = URI.parse('https://api.binance.com/api/v3/ping')

# Define the number of requests to send
num_requests = 10

# Initialize an array to store the response times
response_times = []

num_requests.times do |i|
  start_time = Time.now
  response = Net::HTTP.get_response(binance_api_url)
  end_time = Time.now

  if response.is_a?(Net::HTTPSuccess)
    response_time = ((end_time - start_time) * 1000).to_f # Convert to milliseconds
    response_times << response_time
    puts "Request #{i + 1}: Response Time = #{response_time} ms"
  else
    puts "Request #{i + 1}: Request failed (HTTP #{response.code})"
  end

  #sleep(1) # Wait 1 second between requests (adjust as needed)
end

# Calculate and display statistics
if response_times.empty?
  puts "No successful requests."
else
  average_response_time = response_times.sum / response_times.size
  min_response_time = response_times.min
  max_response_time = response_times.max

  puts "\nRequest Statistics:"
  puts "  Requests: Sent = #{num_requests}, Received = #{response_times.size}, Failed = #{num_requests - response_times.size} (#{((num_requests - response_times.size) / num_requests.to_f * 100).round(2)}% failure)"
  puts "Response Times in milliseconds:"
  puts "  Minimum = #{min_response_time} ms, Maximum = #{max_response_time} ms, Average = #{average_response_time} ms"
end
