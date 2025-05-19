from TestFramework import TestFramework

def main():
    test = TestFramework()

    test.create_configs()
    test.create_folders()

    test.initialize_cluster()

    # Alex Tsagkas Folder
    test.execute_client_command("build/Examples/TreeOps mkdir /alextsagkas/")
    test.execute_client_command("build/Examples/TreeOps --dir=/alextsagkas/ write file_1.txt")
    test.execute_client_command("build/Examples/TreeOps --dir=/alextsagkas/ write file_2.txt")

    # Angelos Motsios Folder
    test.execute_client_command("build/Examples/TreeOps mkdir /angelos_motsios/")
    test.execute_client_command("build/Examples/TreeOps --dir=/angelos_motsios/ write file_1.txt")
    test.execute_client_command("build/Examples/TreeOps --dir=/angelos_motsios/ write file_2.txt")

    # Dump storage
    test.execute_client_command("build/Examples/TreeOps dump")

    test.cleanup()

if __name__ == '__main__':
    main()