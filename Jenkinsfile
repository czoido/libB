ArrayList line_split(String text) {
  return text.split('\\r?\\n').findAll{it.size() > 0} as ArrayList
}

def organization = "conan-ci-cd-training"
def user_channel = "mycompany/stable"
def config_url = "https://github.com/conan-ci-cd-training/settings.git"
def projects = line_split(readTrusted('dependent-projects.txt')).collect { "${it}@${user_channel}" } // TODO: Get list dynamically
def conan_develop_repo = "artifactory-develop"
def artifactory_metadata_repo = "conan-develop-metadata"

String reference_revision = null

def docker_runs = [:] 
docker_runs["conanio-gcc8"] = ["conanio/gcc8", "conanio-gcc8"]	
docker_runs["conanio-gcc7"] = ["conanio/gcc7", "conanio-gcc7"]

def get_stages(id, docker_image, profile, user_channel, config_url, conan_develop_repo, artifactory_metadata_repo) {
    return {
        stage(id) {
            node {
                docker.image(docker_image).inside("--net=host") {
                    def scmVars = checkout scm
                    def repository = scmVars.GIT_URL.tokenize('/')[3].split("\\.")[0]
                    echo("${scmVars}")
                    sh "printenv"
                    withEnv(["CONAN_USER_HOME=${env.WORKSPACE}/conan_cache"]) {
                        def lockfile = "${id}.lock"
                        def buildInfoFilename = "${id}.json"
                        def buildInfo = null
                        try {
                            stage("Configure Conan") {
                                sh "conan --version"
                                sh "conan config install ${config_url}"
                                sh "conan remote add ${conan_develop_repo} http://${env.ARTIFACTORY_URL}/artifactory/api/conan/conan-develop"
                                withCredentials([usernamePassword(credentialsId: 'artifactory-credentials', usernameVariable: 'ARTIFACTORY_USER', passwordVariable: 'ARTIFACTORY_PASSWORD')]) {
                                    sh "conan user -p ${ARTIFACTORY_PASSWORD} -r ${conan_develop_repo} ${ARTIFACTORY_USER}"
                                }
                            }
                            stage("DEPLOY: Start build info: ${env.JOB_NAME} ${env.BUILD_NUMBER}") { 
                                if (env.BRANCH_NAME == "master") {
                                    sh "conan_build_info --v2 start \"${env.JOB_NAME}\" \"${env.BUILD_NUMBER}\""
                                }
                            }
                            stage("Create package") {                                
                                String arguments = "--profile ${profile} --lockfile=${lockfile}"
                                sh "conan graph lock . ${arguments}"
                                sh "conan create . ${user_channel} ${arguments} --build missing --ignore-dirty"
                                //sh "conan upload '*' --all -r ${conan_develop_repo} --confirm  --force"
                                name = sh (script: "conan inspect . --raw name", returnStdout: true).trim()
                                version = sh (script: "conan inspect . --raw version", returnStdout: true).trim()                                
                            }
                            stage("Get created package revision") {
                                // this is some kind of workaround, we have just created the package in the local cache
                                // and search for the package using --revisions to get the revision of the package
                                // write the search to a json file and stash the file to get it after all the builds
                                // have finished to pass it to the dependant projects pipeline
                                if (id=="conanio-gcc8") { //FIX THIS: get just for one of the profiles the revision is the same for all
                                    def search_output = "search_output.json"
                                    sh "conan search ${name}/${version}@${user_channel} --revisions --raw --json=${search_output}"
                                    sh "cat ${search_output}"
                                    stash name: 'full_reference', includes: 'search_output.json'
                                }
                            }
                            stage("DEPLOY: Upload package") {                                
                                if (env.BRANCH_NAME == "master") {
                                    sh "conan upload '*' --all -r ${conan_develop_repo} --confirm  --force"
                                }
                            }
                            stage("DEPLOY: Create build info") {
                                if (env.BRANCH_NAME == "master") {
                                    withCredentials([usernamePassword(credentialsId: 'artifactory-credentials', usernameVariable: 'ARTIFACTORY_USER', passwordVariable: 'ARTIFACTORY_PASSWORD')]) {
                                        sh "conan_build_info --v2 create --lockfile ${lockfile} --user \"\${ARTIFACTORY_USER}\" --password \"\${ARTIFACTORY_PASSWORD}\" ${buildInfoFilename}"
                                        buildInfo = readJSON(file: buildInfoFilename)
                                    }
                                }
                            }
                            stage("DEPLOY: Upload lockfile") {
                                if (env.BRANCH_NAME == "master") {
                                    def lockfile_url = "http://${env.ARTIFACTORY_URL}:8081/artifactory/${artifactory_metadata_repo}/${name}/${version}@${user_channel}/${profile}/conan.lock"
                                    def lockfile_sha1 = sha1(file: lockfile)
                                    withCredentials([usernamePassword(credentialsId: 'artifactory-credentials', usernameVariable: 'ARTIFACTORY_USER', passwordVariable: 'ARTIFACTORY_PASSWORD')]) {
                                        sh "curl --user \"\${ARTIFACTORY_USER}\":\"\${ARTIFACTORY_PASSWORD}\" --header 'X-Checksum-Sha1:'${lockfile_sha1} --header 'Content-Type: application/json' ${lockfile_url} --upload-file ${lockfile}"
                                    }                                
                                }
                            }
                            return buildInfo
                        }
                        finally {
                            deleteDir()
                        }
                    }
                }
            }
        }
    }
}

pipeline {
    agent none
    stages {
        stage('Build') {
            steps {
                script {
                    echo("${currentBuild.fullProjectName.tokenize('/')[0]}")
                    // maybe you want to do different things depending if it is a PR or not?
                    echo("Commit made to ${env.BRANCH_NAME} branch")
                    if (env.TAG_NAME ==~ /release.*/) {
                        echo("This is a commit to master that is tagged with ${env.TAG_NAME}")
                    }
                    docker_runs = withEnv(["CONAN_HOOK_ERROR_LEVEL=40"]) {
                        parallel docker_runs.collectEntries { id, values ->
                          def (docker_image, profile) = values
                            ["${id}": get_stages(id, docker_image, profile, user_channel, config_url, conan_develop_repo, artifactory_metadata_repo)]
                        }
                    }
                }
            }
        }
        // maybe just doing publishes an uploads if we are releasing something
        // or doing a commit to master?
        // maybe if a new tag was created with the name release?
        stage('DEPLOY: Merge and publish build infos') {
            when { branch "master" } 
            steps {
                script {
                   docker.image("conanio/gcc8").inside("--net=host") {
                        def last_info = ""
                        docker_runs.each { id, buildInfo ->
                            writeJSON file: "${id}.json", json: buildInfo
                            if (last_info != "") {
                                sh "conan_build_info --v2 update ${id}.json ${last_info} --output-file mergedbuildinfo.json"
                            }
                            last_info = "${id}.json"
                        }                    
                        sh "cat mergedbuildinfo.json"
                        withCredentials([usernamePassword(credentialsId: 'artifactory-credentials', usernameVariable: 'ARTIFACTORY_USER', passwordVariable: 'ARTIFACTORY_PASSWORD')]) {
                            sh "conan_build_info --v2 publish --url http://${env.ARTIFACTORY_URL}:8081/artifactory --user \"\${ARTIFACTORY_USER}\" --password \"\${ARTIFACTORY_PASSWORD}\" mergedbuildinfo.json"
                        }
                    }
                }
            }
        }

        stage("Trigger dependents jobs") {
            when { branch "master" } 
            agent any
            steps {
                script {
                    unstash 'full_reference'
                    def props = readJSON file: "search_output.json"
                    reference_revision = props[0]['revision']
                    assert reference_revision != null
                    def reference = "${name}/${version}@${user_channel}#${reference_revision}"
                    echo "Full reference: '${reference}'"
                    parallel projects.collectEntries {project_id -> 
                        ["${project_id}": {
                            build(job: "${currentBuild.fullProjectName.tokenize('/')[0]}/jenkins/master", propagate: true, parameters: [
                                [$class: 'StringParameterValue', name: 'reference',    value: reference   ],
                                [$class: 'StringParameterValue', name: 'project_id',   value: project_id  ],
                                [$class: 'StringParameterValue', name: 'organization', value: organization],
                            ])
                        }]
                    }
                }
            }
        }
    }
}