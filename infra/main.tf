terraform {
  required_version = ">= 1.5"
  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "~> 5.0"
    }
  }
}

provider "aws" {
  region = var.region
}

# Latest Ubuntu 24.04 LTS image from Canonical
data "aws_ami" "ubuntu" {
  most_recent = true
  owners      = ["099720109477"]

  filter {
    name   = "name"
    values = ["ubuntu/images/hvm-ssd-gp3/ubuntu-noble-24.04-amd64-server-*"]
  }
}

resource "aws_security_group" "server" {
  name        = "blockchainhttpserver"
  description = "BlockchainHTTPServer: API port and SSH"

  ingress {
    description = "HTTP API"
    from_port   = var.port
    to_port     = var.port
    protocol    = "tcp"
    cidr_blocks = var.api_allowed_cidrs
  }

  ingress {
    description = "SSH"
    from_port   = 22
    to_port     = 22
    protocol    = "tcp"
    cidr_blocks = [var.ssh_allowed_cidr]
  }

  egress {
    from_port   = 0
    to_port     = 0
    protocol    = "-1"
    cidr_blocks = ["0.0.0.0/0"]
  }
}

# Single instance on purpose: the chain lives in process memory, so multiple
# replicas would each mine their own diverging chain.
resource "aws_instance" "server" {
  ami                    = data.aws_ami.ubuntu.id
  instance_type          = var.instance_type
  key_name               = var.ssh_key_name
  vpc_security_group_ids = [aws_security_group.server.id]

  user_data = templatefile("${path.module}/cloud-init.sh.tftpl", {
    repo_url = var.repo_url
    branch   = var.branch
  })
  # Re-create the VM when the boot script changes. Note this wipes the chain.
  user_data_replace_on_change = true

  tags = {
    Name = "blockchainhttpserver"
  }
}

# Stable public IP that survives stop/start
resource "aws_eip" "server" {
  instance = aws_instance.server.id
  domain   = "vpc"
}
