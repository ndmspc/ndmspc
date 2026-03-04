#!/bin/bash
SERVER_URL=${1:-"http://localhost:8080/api/jobs"}

echo -e "\ntest_job1 should change"
echo -e "test_job2 should not change\n"
curl -X GET $SERVER_URL
echo -e "\n"
curl -X POST $SERVER_URL  -H "Content-Type: application/json"   -d '{"name":"test_job1","tasks":[1,2,3]}'
echo -e "\n"
curl -X POST $SERVER_URL  -H "Content-Type: application/json"   -d '{"name":"test_job2","tasks":[1,2,3]}'
echo -e "\n"
curl -X PATCH $SERVER_URL  -H "Content-Type: application/json"   -d '{"name":"test_job1","task":1,"action":"start"}'
echo -e "\n"
curl -X PATCH $SERVER_URL  -H "Content-Type: application/json"   -d '{"name":"test_job1","task":1,"action":"done"}'
echo -e "\n"
curl -X PATCH $SERVER_URL  -H "Content-Type: application/json"   -d '{"name":"test_job1","task":2,"action":"start"}'
echo -e "\n"
curl -X PATCH $SERVER_URL  -H "Content-Type: application/json"   -d '{"name":"test_job1","task":2,"action":"failed"}'
echo -e "\n"
curl -X PATCH $SERVER_URL  -H "Content-Type: application/json"   -d '{"name":"test_job1","task":3,"action":"skipped"}'
echo -e "\n"
curl -X PATCH $SERVER_URL  -H "Content-Type: application/json"   -d '{"name":"test_job2","task":1,"action":"failed"}'
echo -e "\n"
curl -X PATCH $SERVER_URL  -H "Content-Type: application/json"   -d '{"name":"test_job2","task":2,"action":"done"}'
echo -e "\n"
curl -X GET $SERVER_URL
echo -e "\n"