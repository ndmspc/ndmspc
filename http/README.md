# Create password file

```bash
mkdir ~/.globus
read -s -r -p "Enter your secret: " secret && echo -n $secret | base64 > ~/.globus/password.txt
chmod 400 ~/.globus/password.txt
```
