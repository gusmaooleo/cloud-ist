# Exemplos de utilização do cloudIST e outputs esperados

Os exemplos abaixo acompanham o comportamento atual de todos os módulos de
`src/` e dos headers de `include/`. Cada cenário tem a sua própria pasta de
configurações e pode ser executado separadamente, depois da preparação comum.
Cada chamada de `./cloudIST` começa com um datacenter novo.

## Cenário incluído no projeto: o comando do README

O diretório `configs/` contém `demo.conf`, com os comandos:

```text
D small ./inputs ./app.sh 1 1 0.5
R demo small 2 1
L
A demo
L
```

Para executar o cenário, compile o programa a partir dos fontes e execute:

```sh
make
./cloudIST 4 16 500 8 ./configs 3
```

Se um executável copiado de outra máquina terminar imediatamente com `killed`,
reconstrua os artefatos locais com `make clean` seguido de `make`. Neste projeto,
o binário anterior era Intel (`x86_64`), sem assinatura e marcado com quarentena
do macOS. A compilação local produziu um binário nativo `arm64`, com assinatura
válida, que executa normalmente neste ambiente.

**Resultado esperado quando não há `cpulimit` no `PATH`:**

1. O primeiro `L` mostra quatro servidores e uma reserva `demo`, `PENDING`.
   No servidor 1, `demo_1` e `demo_2` estão `RESERVED`; os recursos usados são
   1 vCPU / 2 GB RAM / 2 GB disco, e os disponíveis são 7 vCPUs / 14 GB RAM /
   498 GB disco. Os servidores 2, 3 e 4 têm todos os recursos disponíveis.
2. O comando `A`, na linha 4, imprime o diagnóstico abaixo em stderr. A
   verificação acontece antes de copiar inputs ou lançar processos.
3. O segundo `L` mostra o mesmo estado: a reserva continua `PENDING` e as VMs
   continuam `RESERVED`, sem PID.
4. O programa termina com código de saída 1.

```text
<PROJETO>/configs/demo.conf:4: cpulimit is required to execute VMs; install it and add it to PATH.
```

`<PROJETO>` representa o caminho absoluto desta pasta. `make run` executa esse
mesmo cenário e acrescenta `make: *** [run] Error 1` quando o programa devolve
1. Pode passar outros argumentos com `make run RUN_ARGS='...'`.

O cenário pequeno serve para observar a reserva e o diagnóstico da dependência.
Para executar aplicações neste macOS com limites de memória aceitos pelo
sistema, use os exemplos 3 e 4 abaixo.

## 1. Preparação comum

Execute os comandos na raiz do projeto, na mesma sessão de terminal:

```sh
cd /Users/leonardo/projects/so/SO-26-27-proj-main
make
command -v cpulimit
```

`command -v cpulimit` deve imprimir o caminho do executável. O `cpulimit` precisa
estar no `PATH` para os cenários que usam `A`. Os cenários que só definem e
reservam VMs funcionam sem essa dependência.

Crie os arquivos usados nos exemplos numa pasta temporária:

```sh
EXEMPLOS_DIR=$(mktemp -d /tmp/cloudist-exemplos.XXXXXX)
mkdir -p "$EXEMPLOS_DIR/apps" "$EXEMPLOS_DIR/inputs/base/dados"
mkdir -p "$EXEMPLOS_DIR/01-reserva" "$EXEMPLOS_DIR/02-execucao"
mkdir -p "$EXEMPLOS_DIR/03-reutilizacao" "$EXEMPLOS_DIR/04-capacidade"
mkdir -p "$EXEMPLOS_DIR/05-erros" "$EXEMPLOS_DIR/06-arquivos"

printf '7 5\n' > "$EXEMPLOS_DIR/inputs/base/numeros.txt"
printf 'ola\n' > "$EXEMPLOS_DIR/inputs/base/dados/mensagem.txt"
printf '2\n' > "$EXEMPLOS_DIR/inputs/base/duracao.txt"

cp -R "$EXEMPLOS_DIR/inputs/base" "$EXEMPLOS_DIR/inputs/curta"
cp -R "$EXEMPLOS_DIR/inputs/base" "$EXEMPLOS_DIR/inputs/longa"
printf '1\n' > "$EXEMPLOS_DIR/inputs/curta/duracao.txt"
printf '5\n' > "$EXEMPLOS_DIR/inputs/longa/duracao.txt"

cat > "$EXEMPLOS_DIR/apps/soma.sh" <<'APP'
#!/bin/sh
set -eu

vm_id=${PWD##*/}
read -r primeiro segundo < numeros.txt
read -r mensagem < dados/mensagem.txt
read -r duracao < duracao.txt
soma=$((primeiro + segundo))

printf 'INICIO %s: soma=%s; mensagem=%s\n' "$vm_id" "$soma" "$mensagem"
printf '%s\n' "$soma" > resultado.txt
printf '99 1\n' > numeros.txt
sleep "$duracao"
printf 'FIM %s\n' "$vm_id"
APP

chmod +x "$EXEMPLOS_DIR/apps/soma.sh"
printf 'Pasta dos exemplos: %s\n' "$EXEMPLOS_DIR"
```

A aplicação lê arquivos relativos ao diretório da VM, incluindo um arquivo numa
subpasta. Escreve `resultado.txt` e altera a sua cópia de `numeros.txt`. Assim,
também é possível verificar a cópia recursiva e a preservação do input original.

Nos outputs, `<EXEMPLOS_DIR>` representa o caminho absoluto resolvido da pasta
criada. No macOS, `/tmp/...` pode aparecer como `/private/tmp/...`. `<PID>` é um
número diferente em cada execução. Os blocos identificados como **trechos**
omitem partes repetidas de `L`; as tabelas explicam o estado esperado e não são
impressas pelo programa.

### Sintaxe dos comandos

```text
D <tipo> <pasta-input> <executavel> <RAM-GB> <DISCO-GB> <vCPU>
R <reserva> <tipo> <quantidade> <servidor> [<tipo> <quantidade> <servidor> ...]
A <reserva>
L
E <milissegundos>
H
```

- Em `R`, cada grupo tem **três campos**. O servidor é escolhido explicitamente,
  a partir de `1`; não existe distribuição automática pelos servidores.
- RAM e disco são inteiros. A quantidade de vCPU de uma VM pode ser fracionária,
  com ponto decimal: `0.5`, por exemplo.
- Os caminhos de `D` são relativos ao diretório de onde `cloudIST` foi iniciado,
  e não à pasta do `.conf`. Estes exemplos usam caminhos absolutos.
- Use caminhos sem espaços e comentários em linhas próprias, começando com `#`.
  O parser não implementa aspas de shell nem comentários no fim de `D`, `R`,
  `A` ou `E`.
- `D` e `R` bem-sucedidos não imprimem uma mensagem de confirmação. `L` mostra
  o resultado. `H` imprime uma linha de ajuda.
- Os IDs das VMs seguem `<reserva>_<numero>`, com numeração contínua dentro da
  reserva, independentemente do tipo ou servidor.

### RAM nos exemplos com execução

Nos exemplos 3 e 4, cada VM pede **512 GB** e o servidor tem **1024 GB**. Esses
valores foram escolhidos para permitir a execução neste ambiente macOS: durante
a validação, `setrlimit(RLIMIT_AS, ...)` recusou limites de 120 GB e aceitou
512 GB. A aceitação depende do sistema e dos limites do processo.

O programa usa esse número como capacidade simulada e teto de memória virtual;
não aloca 512 GB de RAM física para iniciar a aplicação. Em Linux, é possível
usar valores menores, ajustando também as contas dos outputs esperados. Os
exemplos de reserva, sem `A`, usam valores menores porque não aplicam os limites
a um processo filho.

## 2. Reserva com dois tipos de VM e dois servidores

Este exemplo mostra uma reserva com quatro VMs, CPU fracionária e colocação
explícita. Ainda não executa a aplicação.

```sh
cat > "$EXEMPLOS_DIR/01-reserva/01-mista.conf" <<EOF
# Tipo: RAM, disco, vCPU
D PEQUENA $EXEMPLOS_DIR/inputs/base $EXEMPLOS_DIR/apps/soma.sh 4 10 0.5
D MEDIA $EXEMPLOS_DIR/inputs/base $EXEMPLOS_DIR/apps/soma.sh 8 20 1.5

# Duas PEQUENA no servidor 1; uma MEDIA e uma PEQUENA no servidor 2
R ex_mista PEQUENA 2 1 MEDIA 1 2 PEQUENA 1 2
L
EOF

./cloudIST 2 16 100 4 "$EXEMPLOS_DIR/01-reserva" 1
```

Cada servidor tem 16 GB de RAM, 100 GB de disco e 4 vCPUs. A última posição,
`1`, indica o máximo de workers que leem configurações.

**Output esperado completo**, substituindo apenas os caminhos:

```text
=================================== DATA CENTER STATUS ===================================

[ PHYSICAL SERVERS ] (2 total)
------------------------------------------------------------------------------------------
 Server #1
   ├─ Resources Total     :   4.00 vCPUs |  16 GB RAM | 100 GB Disk
   ├─ Resources Used      :   1.00 vCPUs |   8 GB RAM |  20 GB Disk
   ├─ Resources Available :   3.00 vCPUs |   8 GB RAM |  80 GB Disk
   └─ Hosted VMs (2):
        ├── VM ID : ex_mista_1       | Type: PEQUENA    | State: RESERVED
        └── VM ID : ex_mista_2       | Type: PEQUENA    | State: RESERVED

 Server #2
   ├─ Resources Total     :   4.00 vCPUs |  16 GB RAM | 100 GB Disk
   ├─ Resources Used      :   2.00 vCPUs |  12 GB RAM |  30 GB Disk
   ├─ Resources Available :   2.00 vCPUs |   4 GB RAM |  70 GB Disk
   └─ Hosted VMs (2):
        ├── VM ID : ex_mista_3       | Type: MEDIA      | State: RESERVED
        └── VM ID : ex_mista_4       | Type: PEQUENA    | State: RESERVED

[ DEFINED VM TYPES ] (2 registered)
------------------------------------------------------------------------------------------
  * Type ID: PEQUENA      | Requirements: 0.50 vCPU, 4 GB RAM, 10 GB Disk
    ├─ Executable : <EXEMPLOS_DIR>/apps/soma.sh
    └─ Input Dir  : <EXEMPLOS_DIR>/inputs/base

  * Type ID: MEDIA        | Requirements: 1.50 vCPU, 8 GB RAM, 20 GB Disk
    ├─ Executable : <EXEMPLOS_DIR>/apps/soma.sh
    └─ Input Dir  : <EXEMPLOS_DIR>/inputs/base


[ RESERVATIONS ] (1 total)
------------------------------------------------------------------------------------------
  * Reservation ID: ex_mista     | State: PENDING  | Total VMs: 4
    ├─ Requested Pairs:
    │    • PEQUENA      : 2 unit(s) on server #1
    │    • MEDIA        : 1 unit(s) on server #2
    │    • PEQUENA      : 1 unit(s) on server #2
    └─ Allocated VMs:
         ├── ID: ex_mista_1      | Host Server: #1
         ├── ID: ex_mista_2      | Host Server: #1
         ├── ID: ex_mista_3      | Host Server: #2
         └── ID: ex_mista_4      | Host Server: #2

================================================================--------------------------
```

A reserva já consome recursos em `PENDING`, com VMs em `RESERVED`. Não existe
PID antes de `A`. Como não foi solicitado `A`, o programa termina depois de ler
o arquivo, sem executar a aplicação nem imprimir um estado adicional.

## 3. Execução completa: reserva, aplicação e limpeza

Aqui a aplicação dura aproximadamente dois segundos. Os três comandos `L`
mostram o estado antes, durante e depois da execução.

```sh
cat > "$EXEMPLOS_DIR/02-execucao/01-ciclo.conf" <<EOF
D SOMA $EXEMPLOS_DIR/inputs/base $EXEMPLOS_DIR/apps/soma.sh 512 10 1
R ex_ciclo SOMA 1 1
L
A ex_ciclo
E 500
L
E 3500
L
EOF

./cloudIST 1 1024 100 4 "$EXEMPLOS_DIR/02-execucao" 1
```

**Estados esperados:**

| Momento | Reserva | VM | Recursos usados: vCPU / RAM / disco | Recursos disponíveis: vCPU / RAM / disco |
| --- | --- | --- | --- | --- |
| Primeiro `L`, antes de `A` | `ex_ciclo`, `PENDING` | `ex_ciclo_1`, `RESERVED`, sem PID | 1 / 512 GB / 10 GB | 3 / 512 GB / 90 GB |
| Segundo `L`, após `E 500` | `ex_ciclo`, `RUNNING` | `ex_ciclo_1`, `RUNNING`, com PID | 1 / 512 GB / 10 GB | 3 / 512 GB / 90 GB |
| Terceiro `L`, após `E 3500` | Nenhuma | Nenhuma | 0 / 0 GB / 0 GB | 4 / 1024 GB / 100 GB |

**Mensagens esperadas**, além das listagens:

```text
Started reservation ex_ciclo (configuration <EXEMPLOS_DIR>/02-execucao/01-ciclo.conf).
INICIO ex_ciclo_1: soma=12; mensagem=ola
FIM ex_ciclo_1
```

**Trechos do segundo `L`:**

```text
   ├─ Resources Used      :   1.00 vCPUs | 512 GB RAM |  10 GB Disk
   ├─ Resources Available :   3.00 vCPUs | 512 GB RAM |  90 GB Disk
   └─ Hosted VMs (1):
        └── VM ID : ex_ciclo_1       | Type: SOMA       | State: RUNNING    | PID: <PID>
```

```text
[ RESERVATIONS ] (1 total)
------------------------------------------------------------------------------------------
  * Reservation ID: ex_ciclo     | State: RUNNING  | Total VMs: 1
```

**Trechos do terceiro `L`:**

```text
   ├─ Resources Used      :   0.00 vCPUs |   0 GB RAM |   0 GB Disk
   ├─ Resources Available :   4.00 vCPUs | 1024 GB RAM | 100 GB Disk
   └─ Hosted VMs (0):
        (None)
```

```text
[ DEFINED VM TYPES ] (1 registered)
```

```text
[ RESERVATIONS ] (0 total)
------------------------------------------------------------------------------------------
 (None)
```

O tipo `SOMA` continua registrado depois de a reserva desaparecer. O programa
não mantém uma VM `TERMINATED` ou uma reserva `FINISHED` na listagem: o reaper
remove a VM terminada e, quando acaba a última VM, remove também a reserva.

Verifique os efeitos no filesystem depois da execução:

```sh
cat "$EXEMPLOS_DIR/inputs/base/numeros.txt"
cat "$EXEMPLOS_DIR/inputs/base/dados/mensagem.txt"
if [ ! -e /tmp/CloudIST/ex_ciclo ]; then
  printf 'Diretorio temporario da reserva removido\n'
fi
```

**Output esperado:**

```text
7 5
ola
Diretorio temporario da reserva removido
```

O input original mantém os seus valores. Durante a execução, a VM trabalha em
`/tmp/CloudIST/ex_ciclo/ex_ciclo_1`; o seu `resultado.txt` e a alteração de
`numeros.txt` desaparecem com a remoção dessa pasta.

`A` inicia os processos e devolve o controlo ao leitor do `.conf`; não espera
pela aplicação. `E 500` suspende esse worker por 500 ms. A ordem visual das
mensagens da aplicação e do cloudIST pode variar, sobretudo com redirecionamento
de stdout, devido ao agendamento dos processos e ao buffering. As esperas dão
margem para observar os estados numa máquina sem sobrecarga; não são barreiras
de sincronização. Se o segundo `L` já mostrar a VM removida, ou o terceiro ainda
a mostrar a correr, ajuste as durações/esperas para o ambiente.

## 4. Liberação parcial e reutilização de recursos

Uma VM dura um segundo e outra cinco segundos. Depois de a primeira terminar,
é possível reservar outra VM no mesmo servidor enquanto a segunda continua a
correr.

```sh
cat > "$EXEMPLOS_DIR/03-reutilizacao/01-reutiliza.conf" <<EOF
D CURTA $EXEMPLOS_DIR/inputs/curta $EXEMPLOS_DIR/apps/soma.sh 512 10 0.5
D LONGA $EXEMPLOS_DIR/inputs/longa $EXEMPLOS_DIR/apps/soma.sh 512 10 1.5
R ex_lote CURTA 1 1 LONGA 1 1
A ex_lote
E 2500
L

R ex_nova CURTA 1 1
L
A ex_nova
E 6000
L
EOF

./cloudIST 1 1024 100 4 "$EXEMPLOS_DIR/03-reutilizacao" 1
```

**Estados esperados no servidor 1:**

| Momento | VMs presentes | Reservas presentes | Usado: vCPU / RAM / disco | Disponível: vCPU / RAM / disco |
| --- | --- | --- | --- | --- |
| Logo depois de `A ex_lote`, antes da primeira terminar | `ex_lote_1` e `ex_lote_2`, `RUNNING` | `ex_lote`, 2 VMs | 2 / 1024 GB / 20 GB | 2 / 0 GB / 80 GB |
| Primeiro `L`, após `E 2500` | Só `ex_lote_2`, `RUNNING` | `ex_lote`, 1 VM | 1.5 / 512 GB / 10 GB | 2.5 / 512 GB / 90 GB |
| Segundo `L`, depois de `R ex_nova` | `ex_lote_2`, `RUNNING`; `ex_nova_1`, `RESERVED` | `ex_lote`, `RUNNING`; `ex_nova`, `PENDING` | 2 / 1024 GB / 20 GB | 2 / 0 GB / 80 GB |
| Terceiro `L`, após `E 6000` | Nenhuma | Nenhuma | 0 / 0 GB / 0 GB | 4 / 1024 GB / 100 GB |

**Trechos do primeiro `L`:**

```text
   ├─ Resources Used      :   1.50 vCPUs | 512 GB RAM |  10 GB Disk
   ├─ Resources Available :   2.50 vCPUs | 512 GB RAM |  90 GB Disk
   └─ Hosted VMs (1):
        └── VM ID : ex_lote_2        | Type: LONGA      | State: RUNNING    | PID: <PID>
```

```text
  * Reservation ID: ex_lote      | State: RUNNING  | Total VMs: 1
    ├─ Requested Pairs:
    │    • CURTA        : 1 unit(s) on server #1
    │    • LONGA        : 1 unit(s) on server #1
    └─ Allocated VMs:
         └── ID: ex_lote_2       | Host Server: #1
```

`Requested Pairs` mantém o pedido original, incluindo `CURTA`. `Total VMs` e
`Allocated VMs` mostram apenas as VMs ainda presentes. A VM sobrevivente mantém
o ID `ex_lote_2`; a remoção da primeira não renumera as restantes.

**Mensagens esperadas**, podendo intercalar-se de forma diferente:

```text
Started reservation ex_lote (configuration <EXEMPLOS_DIR>/03-reutilizacao/01-reutiliza.conf).
INICIO ex_lote_1: soma=12; mensagem=ola
INICIO ex_lote_2: soma=12; mensagem=ola
FIM ex_lote_1
Started reservation ex_nova (configuration <EXEMPLOS_DIR>/03-reutilizacao/01-reutiliza.conf).
INICIO ex_nova_1: soma=12; mensagem=ola
FIM ex_nova_1
FIM ex_lote_2
```

As três aplicações calculam `12`, apesar de cada uma substituir a sua cópia de
`numeros.txt` por `99 1`. Cada VM recebe uma cópia independente do input.
No último `L`, os dois tipos continuam registrados e há zero reservas.

## 5. Reserva recusada sem consumir recursos parcialmente

O primeiro pedido caberia no servidor 1, mas pede três VMs no servidor 2, que só
consegue alojar duas. A reserva inteira é recusada. O pedido seguinte cabe e
demonstra que a tentativa anterior não consumiu recursos.

```sh
cat > "$EXEMPLOS_DIR/04-capacidade/01-capacidade.conf" <<EOF
D MINI $EXEMPLOS_DIR/inputs/base $EXEMPLOS_DIR/apps/soma.sh 2 10 1
R ex_excesso MINI 1 1 MINI 3 2
L
R ex_ok MINI 2 1 MINI 1 2
L
EOF

./cloudIST 2 4 20 2 "$EXEMPLOS_DIR/04-capacidade" 1
```

**Output esperado em stderr:**

```text
Server "2" does not have enough resources.
Could not create reservation ex_excesso from <EXEMPLOS_DIR>/04-capacidade/01-capacidade.conf.
```

**Primeiro `L`:** os dois servidores têm zero recursos usados e zero VMs.
Cada um mantém 2 vCPUs, 4 GB de RAM e 20 GB de disco disponíveis. Há um tipo
registrado e nenhuma reserva:

```text
[ RESERVATIONS ] (0 total)
------------------------------------------------------------------------------------------
 (None)
```

**Segundo `L`:**

| Servidor | VMs `RESERVED` | Usado: vCPU / RAM / disco | Disponível: vCPU / RAM / disco |
| --- | --- | --- | --- |
| 1 | `ex_ok_1`, `ex_ok_2` | 2 / 4 GB / 20 GB | 0 / 0 GB / 0 GB |
| 2 | `ex_ok_3` | 1 / 2 GB / 10 GB | 1 / 2 GB / 10 GB |

**Trecho da reserva criada:**

```text
[ RESERVATIONS ] (1 total)
------------------------------------------------------------------------------------------
  * Reservation ID: ex_ok        | State: PENDING  | Total VMs: 3
```

Mesmo existindo capacidade livre noutro servidor, `R` não muda o servidor pedido
para tentar encaixar uma VM. Neste caso, a validação simula todas as colocações
antes de efetuar a reserva.

## 6. Erros de validação e continuação da leitura

Este cenário inclui um tipo duplicado, um tipo inexistente, quantidade zero,
servidor inválido, reserva duplicada, execução de reserva inexistente, espera
inválida e comando desconhecido. A última listagem comprova que o leitor
continua a processar o arquivo.

```sh
cat > "$EXEMPLOS_DIR/05-erros/01-erros.conf" <<EOF
D MINI $EXEMPLOS_DIR/inputs/base $EXEMPLOS_DIR/apps/soma.sh 2 10 1
D MINI $EXEMPLOS_DIR/inputs/base $EXEMPLOS_DIR/apps/soma.sh 2 10 1
R ex_sem_tipo AUSENTE 1 1
R ex_zero MINI 0 1
R ex_servidor MINI 1 3
R ex_valida MINI 1 1
R ex_valida MINI 1 2
A ex_inexistente
E -1
X
L
EOF

./cloudIST 2 4 20 2 "$EXEMPLOS_DIR/05-erros" 1
printf 'Codigo de saida: %s\n' "$?"
```

**Output esperado em stderr**, nesta ordem:

```text
VM type with id MINI already exists.
Could not define VM type MINI from <EXEMPLOS_DIR>/05-erros/01-erros.conf.
VM of type AUSENTE does not exist
Could not create reservation ex_sem_tipo from <EXEMPLOS_DIR>/05-erros/01-erros.conf.
Reservation item must request at least one VM.
Could not create reservation ex_zero from <EXEMPLOS_DIR>/05-erros/01-erros.conf.
Invalid server in reservation.
Could not create reservation ex_servidor from <EXEMPLOS_DIR>/05-erros/01-erros.conf.
Reservation with id ex_valida already exists.
Could not create reservation ex_valida from <EXEMPLOS_DIR>/05-erros/01-erros.conf.
Reservation ID ex_inexistente not found.
Could not start reservation ex_inexistente from <EXEMPLOS_DIR>/05-erros/01-erros.conf.
Invalid E command in <EXEMPLOS_DIR>/05-erros/01-erros.conf.
Invalid command in <EXEMPLOS_DIR>/05-erros/01-erros.conf.
```

**Estado esperado no último `L`:**

- Um único tipo registrado: `MINI`.
- Uma única reserva: `ex_valida`, `PENDING`, com `ex_valida_1` em `RESERVED` no
  servidor 1.
- Servidor 1: usado 1 vCPU / 2 GB RAM / 10 GB disco; disponível 1 vCPU /
  2 GB RAM / 10 GB disco.
- Servidor 2: nenhuma VM, todos os recursos disponíveis.

Depois da listagem, o comando `printf` imprime:

```text
Codigo de saida: 1
```

O programa continua a ler o arquivo depois de um comando inválido, mas propaga
a ocorrência de erros para o código de saída de `main`. Falhas de execução
das VMs também fazem o programa devolver 1. Verifique stderr e o estado do
datacenter para identificar a falha. Erros nos argumentos de arranque impedem
a leitura das configurações:

```sh
./cloudIST 1 16 100 4 "$EXEMPLOS_DIR/05-erros" 0
printf 'Codigo de saida: %s\n' "$?"
```

**Output esperado:**

```text
Invalid command line arguments.
Codigo de saida: 1
```

A primeira linha vai para stderr e a segunda para stdout. Não se lê nenhum
`.conf`, porque `MAX_THREADS` tem de ser maior do que zero.

## 7. Dois arquivos: ordem alfabética e workers concorrentes

Crie dois arquivos independentes. Cada um define o seu próprio tipo antes de o
reservar, e cada um usa um servidor diferente.

```sh
cat > "$EXEMPLOS_DIR/06-arquivos/01-primeiro.conf" <<EOF
D TIPO_A $EXEMPLOS_DIR/inputs/base $EXEMPLOS_DIR/apps/soma.sh 4 10 0.5
R ex_a TIPO_A 1 1
L
EOF

cat > "$EXEMPLOS_DIR/06-arquivos/02-segundo.conf" <<EOF
D TIPO_B $EXEMPLOS_DIR/inputs/base $EXEMPLOS_DIR/apps/soma.sh 8 20 1.5
R ex_b TIPO_B 1 2
L
EOF

./cloudIST 2 16 100 4 "$EXEMPLOS_DIR/06-arquivos" 1
```

**Output esperado com um worker:**

| Listagem | Tipos registrados | Reservas | Servidor 1: usado / disponível | Servidor 2: usado / disponível |
| --- | --- | --- | --- | --- |
| Primeiro `L`, em `01-primeiro.conf` | `TIPO_A` | `ex_a`, `PENDING` | 0.5 vCPU, 4 GB RAM, 10 GB disco / 3.5 vCPU, 12 GB RAM, 90 GB disco | 0 vCPU, 0 GB RAM, 0 GB disco / 4 vCPU, 16 GB RAM, 100 GB disco |
| Segundo `L`, em `02-segundo.conf` | `TIPO_A`, `TIPO_B` | `ex_a` e `ex_b`, `PENDING` | 0.5 vCPU, 4 GB RAM, 10 GB disco / 3.5 vCPU, 12 GB RAM, 90 GB disco | 1.5 vCPU, 8 GB RAM, 20 GB disco / 2.5 vCPU, 8 GB RAM, 80 GB disco |

**Trecho da última listagem:**

```text
[ RESERVATIONS ] (2 total)
------------------------------------------------------------------------------------------
  * Reservation ID: ex_a         | State: PENDING  | Total VMs: 1
    ├─ Requested Pairs:
    │    • TIPO_A       : 1 unit(s) on server #1
    └─ Allocated VMs:
         └── ID: ex_a_1          | Host Server: #1

  * Reservation ID: ex_b         | State: PENDING  | Total VMs: 1
    ├─ Requested Pairs:
    │    • TIPO_B       : 1 unit(s) on server #2
    └─ Allocated VMs:
         └── ID: ex_b_1          | Host Server: #2
```

Agora execute a mesma pasta com dois workers:

```sh
./cloudIST 2 16 100 4 "$EXEMPLOS_DIR/06-arquivos" 2
```

**Resultado esperado com dois workers:**

- A última listagem contém as duas reservas e os mesmos recursos usados e
  disponíveis da segunda linha da tabela anterior.
- `ex_a_1` está no servidor 1 e `ex_b_1` no servidor 2, ambas em `RESERVED`.
- A primeira listagem pode mostrar um estado intermédio diferente, incluindo
  ambos os tipos ou ambas as reservas.
- A ordem de registro dos tipos e das reservas pode variar.

A fila de arquivos é ordenada alfabeticamente, mas dois workers podem avançar
pelos comandos em ritmos diferentes. A ordenação da fila não garante que todos
os comandos de `01-primeiro.conf` terminem antes de começar `02-segundo.conf`.
Use um worker quando um arquivo depende de tipos ou reservas criados por outro.
O limite de workers controla a leitura de arquivos; um único worker pode lançar
vários processos de VM com `A`.

## 8. Como comparar stdout e stderr

Para guardar as duas saídas de um cenário separadamente:

```sh
./cloudIST 2 4 20 2 "$EXEMPLOS_DIR/05-erros" 1 \
  > "$EXEMPLOS_DIR/05-erros/stdout.txt" \
  2> "$EXEMPLOS_DIR/05-erros/stderr.txt"

cat "$EXEMPLOS_DIR/05-erros/stderr.txt"
```

Somente os arquivos regulares terminados em `.conf`, diretamente dentro da
pasta indicada, são processados. Os `.txt` acima são ignorados; não há procura
recursiva de configurações.

Nos exemplos com `A`, o PID listado é o do processo iniciado pelo cloudIST,
que executa o `cpulimit`; esse processo lança a aplicação. `Started reservation`
confirma o lançamento, mas a aplicação ainda pode falhar depois disso. Uma
falha ao aplicar os limites ou o diretório de trabalho pode produzir:

```text
cloudIST: failed to apply VM limits or working directory
VM ex_ciclo_1 (PID <PID>) exited with an error.
```

Os recursos também são devolvidos quando o filho termina com erro. Se o
`cpulimit` não estiver no `PATH` ao processar `A`, o programa mantém a reserva
pendente e emite um erro com o arquivo e a linha do comando, por exemplo:

```text
<EXEMPLOS_DIR>/02-execucao/01-ciclo.conf:4: cpulimit is required to execute VMs; install it and add it to PATH.
```

Caso a dependência desapareça entre a verificação e a execução, o filho ainda
pode emitir:

```text
cloudIST: unable to execute cpulimit or VM executable
VM ex_ciclo_1 (PID <PID>) exited with an error.
```

Essas saídas não são o resultado de sucesso dos exemplos 3 e 4.

## 9. Limites e observações da implementação

- O disco reservado participa das contas de capacidade, mas o limite aplicado
  ao processo é `RLIMIT_FSIZE`, isto é, tamanho máximo de **cada arquivo**;
  não é uma quota do total da pasta da VM.
- A RAM é aplicada com `RLIMIT_AS`, como limite de espaço de endereçamento.
- O limite passado ao `cpulimit` é calculado por
  `(vCPU_da_VM / CPU_por_servidor) * numero_de_servidores * 100`, arredondado
  ao inteiro mais próximo e com mínimo de 1%. Isso segue a implementação atual.
  No exemplo 3: `(1 / 4) * 1 * 100 = 25%`.
- O programa espera pelos processos de VM antes de terminar, mesmo que os
  workers já tenham acabado de ler os arquivos. Não imprime automaticamente
  uma última listagem; é preciso incluir `L` no momento pretendido.
- IDs de tipos e reservas podem ter até 39 caracteres. Os limites atuais são
  128 tipos, 256 reservas simultâneas, 32 grupos por `R`, 128 VMs por reserva e
  128 VMs por servidor.

Para voltar a executar qualquer exemplo, basta repetir a respetiva chamada
de `./cloudIST`. As configurações permanecem em `$EXEMPLOS_DIR`, e as aplicações
trabalham sobre novas cópias do input original.

Referências locais: `src/main.c` (fila, workers e mensagens), `src/parser.c`
(sintaxe), `src/datacenter.c` (estados, listagens e reaper),
`src/datacenter_utils.c` (validação, IDs e limites), `src/filesystem.c`
(cópia e limpeza), `src/resources.c` (contas de recursos), `include/constants.h`
(capacidades máximas) e `Makefile` (compilação). A sintaxe da dependência foi
conferida na [documentação do cpulimit](https://github.com/opsengine/cpulimit/blob/master/README.md).
