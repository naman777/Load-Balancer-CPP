const http = require('http');

// Configuration
const loadBalancerUrl = 'http://127.0.0.1:8080'; // Load balancer address
const numRequests = 50; // Number of requests to send

// Function to make an HTTP request
function makeRequest(requestNumber) {
  return new Promise((resolve, reject) => {
    const req = http.get(loadBalancerUrl, (res) => {
      let data = '';
      res.on('data', (chunk) => {
        data += chunk;
      });
      res.on('end', () => {
        console.log(`Response #${requestNumber}: ${data.trim()}`);
        resolve(data.trim());
      });
    });

    req.on('error', (err) => {
      console.error(`Request #${requestNumber} failed: ${err.message}`);
      reject(err);
    });

    req.end();
  });
}

// Main function to send multiple requests
async function sendRequests() {
  console.log(`Sending ${numRequests} requests to ${loadBalancerUrl}...`);
  const promises = [];

  for (let i = 1; i <= numRequests; i++) {
    promises.push(makeRequest(i));
  }

  try {
    await Promise.all(promises);
    console.log('All requests completed.');
  } catch (error) {
    console.error('An error occurred:', error);
  }
}

// Run the test
sendRequests();

