
lspcs
deploy -a unikraft-nginx.img -pc 0 -peers all
deploy -a unikraft-c-nginx-client.img -pc 1 -peers 0

suspend -i 0


sandbox0:
curl -v http://localhost:8080/
sandbox1:
curl -v http://localhost:8081/


resume -i 0

set-acl -i 2 0 -v 0

deploy -a unikraft-c-nginx-client.img -pc 2 -peers 1
set-acl -i 2 0 -v 1


deploy -a unikraft-sqlite.img -pc 3 -peers 0

Ctrl + \ + id

CREATE TABLE demo (id INTEGER PRIMARY KEY, message TEXT);
SELECT * FROM demo;
INSERT INTO demo VALUES (1, 'hello from a protocon');
SELECT * FROM demo;
