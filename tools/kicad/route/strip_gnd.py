import re,sys
s=open(sys.argv[1]).read()
i=s.index('(net GND\n'); d=0
for j in range(i,len(s)):
    if s[j]=='(': d+=1
    elif s[j]==')':
        d-=1
        if d==0: break
s=s[:i]+s[j+1:]
s=re.sub(r'(\(class [^\n]*?) GND( |\n)', r'\1\2', s)
open(sys.argv[2],'w').write(s)
print(s.count('(net GND'), 'GND nets left;', len(re.findall(r'\(class[^()]*\bGND\b',s)),'class refs')
