const express = require('express');

// Function to create an Express server
function createServer(port) {
  const app = express();

  app.get('/', (req, res) => {
    res.send(`Response from server running on port ${port}`);
  });

  app.listen(port, () => {
    console.log(`Server is running on port ${port}`);
  });
}

// Ports to run the servers on
const ports = [8001, 8002, 8003];

// Start servers on each port
ports.forEach(port => createServer(port));

